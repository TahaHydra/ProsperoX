// GPL-2.0; adapted synthetic fixtures from KytyPS5 PR #362 and commit
// c913951f7b. Source provenance is recorded in
// docs/phase4-shader-conformance.md. Original backend integration fixture:
// distinct images, types and dimensions are selected by a nonuniform runtime
// key. Host materialization is tested separately.
void RunPhase4IndirectImages(VulkanHarness &vulkan) {
  using namespace ShaderRecompiler::IR;
  constexpr u32 count = 40;
  TestCase test;
  test.name = "Phase4IndirectImages";
  test.has_compute_info = true;
  test.compute_info.wave_size = 64;
  test.compute_info.threads_num[0] = 64;
  test.compute_info.threads_num[1] = test.compute_info.threads_num[2] = 1;
  test.compute_info.thread_ids_num = 1;
  test.initial.resize(64);
  AppendVMovU32(&test.code, 20, 0);
  AppendVMovU32(&test.code, 21, 0x3f800000u);
  AppendVMovU32(&test.code, 22, 0x3f800000u);
  test.code.push_back(EncodeMimg0(0x24, 1));
  test.code.push_back(EncodeMimg1(4, 20));
  AppendStoreVgprAtLaneDwordOffset(&test.code, 4, 0, 0);
  AppendEnd(&test.code);
  test.opcodes = {ShaderOpcode::V_MOV_B32, ShaderOpcode::IMAGE_SAMPLE,
                  ShaderOpcode::BUFFER_STORE_DWORD, ShaderOpcode::S_ENDPGM};
  auto compiled = CompileCase(test, vulkan.SubgroupSize());
  auto &program = compiled.program;
  Require(test.name, "fixture", program.info.images.size() == 1,
          "expected one root image");
  const auto base = program.info.images.front();
  program.info.images.assign(count, base);
  for (u32 i = 0; i < count; ++i) {
    auto &image = program.info.images[i];
    image.indirect_root = 0;
    image.numeric_class = i % 2 ? Prospero::TextureNumericClass::Uint
                                : Prospero::TextureNumericClass::Float;
    image.dimension = i % 3 ? ShaderRecompiler::Decoder::ImageDimension::Dim2D
                            : ShaderRecompiler::Decoder::ImageDimension::Dim1D;
    image.indirect_resources.clear();
  }
  auto &root = program.info.images[0];
  root.indirect_mapping_offset = 0;
  root.indirect_search_iterations = std::bit_width(count);
  compiled.resources.flattened_srt = {count};
  for (u32 i = 0; i < count; ++i) {
    root.indirect_resources.push_back(i);
    compiled.resources.flattened_srt.push_back(i);
    compiled.resources.flattened_srt.push_back(i);
  }
  program.descriptor_sources[base.source].indirect_image =
      DescriptorSource::IndirectImage{0, 0, 224, 12, 0};
  for (auto *block : program.blocks) {
    for (auto it = block->begin(); it != block->end(); ++it) {
      if (it->GetOpcode() != ValueOpcode::GetImageResource)
        continue;
      auto lane = block->PrependNewInst(it, ValueOpcode::LaneId, {});
      auto less = block->PrependNewInst(it, ValueOpcode::ULessThan32,
                                        {Value(&*lane), Value(count)});
      auto sub = block->PrependNewInst(it, ValueOpcode::ISub32,
                                       {Value(&*lane), Value(count)});
      auto key =
          block->PrependNewInst(it, ValueOpcode::SelectU32,
                                {Value(&*less), Value(&*lane), Value(&*sub)});
      it->SetArg(0, Value(&*key));
    }
  }
  program.binding_layout_complete = false;
  AllocateBindings(program);
  auto compute = test.compute_info;
  compute.host_subgroup_size = vulkan.SubgroupSize();
  ShaderRecompiler::Spirv::AnalyzeProgramRequirements(program);
  compiled.spirv =
      ShaderRecompiler::Spirv::EmitProgram(program, {.compute = &compute});
  ValidateSpirv(test.name, compiled.spirv);
  std::vector<VulkanHarness::Image> images;
  for (u32 i = 0; i < count; ++i) {
    const u32 value = i % 2 ? 100 + i : std::bit_cast<u32>(float(100 + i));
    std::vector<std::vector<u32>> mips{std::vector<u32>(i % 3 ? 4 : 2, value)};
    if (i % 4)
      mips.push_back({i % 2 ? 200 + i : std::bit_cast<u32>(float(200 + i))});
    images.push_back(vulkan.CreateImageMips(
        test.name, 2, i % 3 ? 2 : 1,
        i % 2 ? vk::Format::eR32Uint : vk::Format::eR32Sfloat,
        vk::ImageUsageFlagBits::eSampled, mips, 1,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        i % 3 ? vk::ImageType::e2D : vk::ImageType::e1D,
        i % 3 ? vk::ImageViewType::e2D : vk::ImageViewType::e1D, 1));
  }
  auto sampler = vulkan.CreateNearestSampler(test.name, 32.0f);
  auto output = vulkan.CreateStorageBuffer(test.name, test.initial, 64);
  for (u32 pass = 0; pass < 2; ++pass) {
    test.expected.clear();
    for (u32 lane = 0; lane < 64; ++lane) {
      const auto selected = lane % count;
      const auto actual = pass ? count - 1 - selected : selected;
      const u32 value = (actual % 4 ? 200 : 100) + actual;
      test.expected.push_back(actual % 2 ? value
                                         : std::bit_cast<u32>(float(value)));
    }
    // A second dispatch changes the runtime key mapping without recompiling
    // SPIR-V.
    if (pass) {
      for (u32 i = 0; i < count; ++i)
        compiled.resources.flattened_srt[2 + i * 2] = count - 1 - i;
    }
    vulkan.Dispatch(test, compiled, output, nullptr, nullptr, nullptr, nullptr,
                    sampler, images);
    CompareWords(test, "mixed candidate readback", test.expected,
                 vulkan.ReadBuffer(test.name, output, 64));
  }
  vulkan.Device().destroySampler(sampler);
  for (auto &image : images)
    vulkan.DestroyImage(&image);
  vulkan.DestroyBuffer(&output);
  std::printf("PHASE4_IMAGES_PASS 40 distinct typed candidates, runtime lane "
              "selection\n");
}

TestCase Fp16RoundNearestEvenConversion() {
  using O = ShaderOpcode;

  std::vector<u32> code;
  AppendVMovLiteral(&code, 0, 0x3f803000u); // tie: 0x3c01/0x3c02
  AppendVMovLiteral(&code, 1, 0x33c00000u); // tie: 0x0001/0x0002
  AppendVMovLiteral(&code, 2, 0x33000000u); // tie: 0x0000/0x0001
  AppendVMovLiteral(&code, 3, 0xb3c00000u); // negative subnormal tie
  code.push_back(EncodeVop1(0x0a, 10, Vgpr(0)));
  code.push_back(EncodeVop1(0x0a, 11, Vgpr(1)));
  code.push_back(EncodeVop1(0x0a, 12, Vgpr(2)));
  code.push_back(EncodeVop1(0x0a, 13, Vgpr(3)));

  AppendVMovLiteral(&code, 4, 0x3c013c01u);
  AppendVMovLiteral(&code, 5, 0x10001000u);
  AppendVop3p(&code, 0x0f, 14, Vgpr(4), Vgpr(5), 0, 0x3);
  for (u32 i = 0; i < 5; i++) {
    AppendStoreVgpr(&code, 10 + i, i);
  }
  AppendEnd(&code);

  TestCase test;
  test.name = "Fp16RoundNearestEvenConversion";
  test.code = std::move(code);
  test.expected = {0x00003c02u, 0x00000002u, 0x00000000u, 0x00008002u,
                   0x3c023c02u};
  test.opcodes = {O::V_MOV_B32, O::V_CVT_F16_F32, O::V_PK_ADD_F16,
                  O::BUFFER_STORE_DWORD, O::S_ENDPGM};
  test.forbidden_spirv = {"PackHalf2x16"};
  return test;
}

// Independent numerical oracle: search the ordered set of representable finite
// binary16 values. It shares no shift/rounding implementation with the emitter.
TestCase Phase4Fp16BoundaryOracle() {
  std::vector<double> half;
  for (u32 h = 0; h < 0x7c00; ++h) {
    const auto exponent = (h >> 10) & 31;
    half.push_back(std::ldexp(double((h & 1023) + (exponent ? 1024 : 0)),
                              exponent ? int(exponent) - 25 : -24));
  }
  std::vector<float> inputs{0.0f,
                            -0.0f,
                            65520.0f,
                            -65520.0f,
                            std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity()};
  for (u32 h : {0u, 1u, 2u, 0x3feu, 0x3ffu, 0x400u, 0x3555u, 0x3bffu, 0x3c01u,
                0x7bfeu}) {
    const float midpoint = float((half[h] + half[h + 1]) / 2);
    for (float x :
         {std::nextafter(midpoint, 0.0f), midpoint,
          std::nextafter(midpoint, std::numeric_limits<float>::infinity())}) {
      inputs.push_back(x);
      inputs.push_back(-x);
    }
  }
  TestCase test;
  test.name = "Phase4Fp16BoundaryOracle";
  for (u32 i = 0; i < inputs.size(); ++i) {
    const auto value = inputs[i];
    const auto magnitude = std::abs(double(value));
    u32 expected = 0;
    if (magnitude >= 65520)
      expected = 0x7c00;
    else {
      auto hi = std::lower_bound(half.begin(), half.end(), magnitude);
      if (hi == half.end())
        expected = 0x7bff;
      else if (hi == half.begin())
        expected = 0;
      else {
        const u32 upper = u32(hi - half.begin());
        const auto below = magnitude - half[upper - 1];
        const auto above = half[upper] - magnitude;
        expected = below < above || (below == above && (upper & 1)) ? upper - 1
                                                                    : upper;
      }
    }
    if (std::signbit(value))
      expected |= 0x8000;
    test.expected.push_back(expected);
    AppendVMovLiteral(&test.code, 1, std::bit_cast<u32>(value));
    test.code.push_back(EncodeVop1(0x0a, 2, Vgpr(1)));
    AppendStoreVgpr(&test.code, 2, i);
  }
  AppendEnd(&test.code);
  test.opcodes = {ShaderOpcode::V_MOV_B32, ShaderOpcode::V_CVT_F16_F32,
                  ShaderOpcode::BUFFER_STORE_DWORD, ShaderOpcode::S_ENDPGM};
  return test;
}

TestCase ScalarSubvectorLoops(u32 wave_size) {
  using O = ShaderOpcode;
  using namespace ShaderRecompiler::Decoder;
  TestCase test;
  test.name = wave_size == 64 ? "ScalarSubvectorLoopsWave64"
                              : "ScalarSubvectorLoopsWave32";
  constexpr u32 sentinel = 0x5a13abcdu;
  constexpr u32 low = 0x80000005u, high = 0x42000002u;
  struct Case {
    u32 lo, hi, body_mode, saved, final_lo, final_hi, passes;
  };
  constexpr Case cases[] = {
      {0, 0, 0, sentinel, 0, 0, 0},
      {low, 0, 0, 0, low, 0, 1},
      {0, high, 0, 0, 0, high, 1},
      {low, high, 0, low, low, high, 2},
      {~0u, ~0u, 0, ~0u, ~0u, ~0u, 2},
      // Restore full EXEC before END to exercise its HI != 0 arm.
      {low, high, 1, high, high, ~0u, 1},
      // END must restore the low mask saved after the body has changed it.
      {low, high, 2, 0, 0, high, 2},
      // BEGIN EXEC_LO first copies HI to its aliased destination, then clears
      // HI.
      {low, high, 3, 0, 0, high, 2},
  };
  auto &code = test.code;
  for (u32 index = 0; index < std::size(cases); ++index) {
    const auto &item = cases[index];
    const u32 scc = index & 1u;
    const u32 saved_reg = item.body_mode == 3 ? 126u : 38u;
    AppendVMovU32(&code, 2, 0);
    AppendVMovU32(&code, 3, 0);
    AppendSMovLiteral(&code, 38, sentinel);
    AppendSMovLiteral(&code, 32, 1); // Scalar 2^passes; MULK preserves SCC.
    AppendSMovLiteral(&code, 33, 0xff);
    AppendSMovLiteral(&code, 126, item.lo);
    AppendSMovLiteral(&code, 127, item.hi);
    code.push_back(EncodeSopc(0x06, InlineU32(0), InlineU32(scc == 0 ? 1 : 0)));
    const auto begin_word = static_cast<u32>(code.size());
    code.push_back(0);
    const auto body_word = static_cast<u32>(code.size());
    code.push_back(
        EncodeSMovB32(33, 253)); // Observe SCC immediately after BEGIN.
    code.push_back(EncodeSopk(0x10, 32, 2));
    code.push_back(EncodeVop2(0x25, 2, InlineU32(1), 2));
    code.push_back(EncodeVop1(0x01, 3, 32));
    if (item.body_mode == 1) {
      code.push_back(EncodeSop1(0x04, 126, 193)); // S_MOV_B64 EXEC, -1.
    } else if (item.body_mode == 2) {
      code.push_back(EncodeSMovB32(126, InlineU32(0)));
    }
    const auto end_word = static_cast<u32>(code.size());
    code.push_back(EncodeSopk(0x1c, saved_reg, body_word - end_word - 1u));
    code[begin_word] = EncodeSopk(0x1b, saved_reg, end_word - begin_word);
    const u32 sources[] = {saved_reg, 126, 127, 253, 32, 33};
    for (u32 i = 0; i < std::size(sources); ++i) {
      code.push_back(EncodeSMovB32(20 + i, sources[i]));
    }
    code.push_back(
        EncodeSop1(0x04, 126, 193)); // Restore all lanes for readback.
    const u32 expected[] = {item.saved,        item.final_lo,
                            item.final_hi,     scc,
                            1u << item.passes, item.passes == 0 ? 0xff : scc};
    for (u32 i = 0; i < std::size(expected); ++i) {
      AppendStoreSgprAtLaneDwordOffset(&code, 20 + i, 0,
                                       static_cast<u32>(test.expected.size()));
      test.expected.insert(test.expected.end(), wave_size, expected[i]);
    }
    for (u32 reg : {2u, 3u}) {
      AppendStoreVgprAtLaneDwordOffset(&code, reg, 0,
                                       static_cast<u32>(test.expected.size()));
      for (u32 lane = 0; lane < wave_size; ++lane) {
        const bool upper = lane >= 32;
        const u32 mask = upper || item.body_mode == 3 ? item.hi : item.lo;
        const bool active = ((mask >> (lane % 32)) & 1u) != 0 &&
                            !(upper && item.body_mode == 1);
        const u32 pass_value = upper && item.lo != 0 ? 4u : 2u;
        test.expected.push_back(active ? (reg == 2 ? 1u : pass_value) : 0u);
      }
    }
  }
  AppendEnd(&code);
  test.initial.resize(test.expected.size());
  test.opcodes = {O::S_SUBVECTOR_LOOP_BEGIN,
                  O::S_SUBVECTOR_LOOP_END,
                  O::S_MOV_B32,
                  O::S_MOV_B64,
                  O::S_MULK_I32,
                  O::S_CMP_EQ_U32,
                  O::V_MOV_B32,
                  O::V_ADD_NC_U32,
                  O::V_LSHLREV_B32,
                  O::BUFFER_STORE_DWORD,
                  O::S_ENDPGM};
  test.compute_info.threads_num[0] = wave_size;
  test.compute_info.threads_num[1] = test.compute_info.threads_num[2] = 1;
  test.compute_info.thread_ids_num = 1;
  test.compute_info.wave_size = wave_size;
  test.has_compute_info = true;
  return test;
}

// Four independent guest waves share LDS. Different lane data makes accidental
// communication with the adjacent physical half observable at every readback.
TestCase Phase4Wave32MultiwaveMemory() {
  auto test = Wave64CrossHalfLaneAndLds();
  test.name = "Phase4Wave32MultiwaveMemory";
  test.code.clear();
  test.expected.clear();
  auto &code = test.code;
  AppendVMovU32(&code, 1, 100);
  code.push_back(EncodeVop2(0x25, 1, Vgpr(0), 1));
  AppendVop3(&code, 0x360, 8, Vgpr(1), InlineU32(17));
  AppendStoreSgprAtLaneDwordOffset(&code, 8, 0, 0);
  code.push_back(EncodeVop2(0x1b, 2, InlineU32(31), 0));
  code.push_back(EncodeVopc(0xc2, InlineU32(9), 2));
  code.push_back(EncodeSop1(0x04, 126, 106));
  code.push_back(EncodeVop1(0x02, 9, Vgpr(1)));
  code.push_back(EncodeSop1(0x04, 126, 193));
  AppendStoreSgprAtLaneDwordOffset(&code, 9, 0, 128);
  AppendStoreSgprAtLaneDwordOffset(&code, 106, 0, 256);
  AppendStoreSgprAtLaneDwordOffset(&code, 107, 0, 384);
  code.push_back(EncodeVop2(0x1a, 2, InlineU32(2), 0));
  code.push_back(EncodeDs0(0x0d));
  code.push_back(EncodeDs1(0, 1, 2));
  code.push_back(EncodeSopp(0x0a, 0));
  code.push_back(EncodeVop2(0x1d, 3, InlineU32(64), 0));
  code.push_back(EncodeVop2(0x1a, 3, InlineU32(2), 3));
  code.push_back(EncodeDs0(0x36));
  code.push_back(EncodeDs1(4, 0, 3));
  AppendStoreVgprAtLaneDwordOffset(&code, 4, 0, 512);
  AppendEnd(&code);
  for (u32 wave = 0; wave < 4; ++wave)
    test.expected.insert(test.expected.end(), 32, 117 + wave * 32);
  for (u32 wave = 0; wave < 4; ++wave)
    test.expected.insert(test.expected.end(), 32, 109 + wave * 32);
  test.expected.insert(test.expected.end(), 128, 1u << 9);
  test.expected.insert(test.expected.end(), 128, 0);
  for (u32 lane = 0; lane < 128; ++lane)
    test.expected.push_back(100 + (lane ^ 64));
  test.compute_info.wave_size = 32;
  return test;
}
