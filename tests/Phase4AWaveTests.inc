// Original synthetic RDNA2 fixtures. Expected results use scalar host
// arithmetic from AMD's architectural contracts, never the emulator's
// translator/evaluator.
TestCase Phase4ASetup(const char *name, u32 wave, u32 threads) {
  TestCase test;
  test.name = name;
  test.has_compute_info = true;
  test.compute_info.wave_size = wave;
  test.compute_info.threads_num[0] = threads;
  test.compute_info.threads_num[1] = test.compute_info.threads_num[2] = 1;
  test.compute_info.thread_ids_num = 1;
  return test;
}

void Phase4AStore(TestCase &test, u32 reg, u32 threads, u32 expected) {
  AppendStoreSgprAtLaneDwordOffset(&test.code, reg, 0,
                                   u32(test.expected.size()));
  test.expected.insert(test.expected.end(), threads, expected);
}

TestCase Phase4ABranchFlags(u32 wave, u32 threads) {
  auto test = Phase4ASetup("Phase4ABranchFlags", wave, threads);
  auto &code = test.code;
  constexpr uint64_t masks[] = {
      0, 1, 0x80000000ull, 0x100000000ull, 0x8000000000000000ull, ~uint64_t{0}};
  for (const auto mask : masks) {
    for (u32 branch = 6; branch <= 9; ++branch) {
      // Distinct EXEC and VCC values detect testing the wrong register.
      const uint64_t exec = branch < 8 ? ~mask : mask;
      const uint64_t vcc = branch < 8 ? mask : ~mask;
      AppendSMovLiteral(&code, 106, u32(vcc));
      AppendSMovLiteral(&code, 107, u32(vcc >> 32));
      AppendSMovLiteral(&code, 126, u32(exec));
      AppendSMovLiteral(&code, 127, u32(exec >> 32));
      code.push_back(EncodeSMovB32(21, 251));
      code.push_back(EncodeSMovB32(22, 252));
      code.push_back(EncodeSopp(branch, 2));
      code.push_back(EncodeSMovB32(20, InlineU32(11)));
      code.push_back(EncodeSopp(2, 1));
      code.push_back(EncodeSMovB32(20, InlineU32(42)));
      // Restore all guest lanes before observing scalar results.
      code.push_back(EncodeSop1(4, 126, 193));
      const auto active_mask = wave == 32 ? uint64_t{UINT32_MAX} : UINT64_MAX;
      const bool nonzero = (mask & active_mask) != 0;
      Phase4AStore(test, 20, threads,
                   ((branch & 1) ? nonzero : !nonzero) ? 42 : 11);
      Phase4AStore(test, 21, threads, (vcc & active_mask) == 0);
      Phase4AStore(test, 22, threads, (exec & active_mask) == 0);
    }
  }
  AppendEnd(&code);
  return test;
}

TestCase Phase4ASaveExec(u32 wave, u32 threads) {
  auto test = Phase4ASetup("Phase4ASaveExec", wave, threads);
  auto &code = test.code;
  uint32_t seed = 0x505834a;
  const auto next = [&]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  const auto next64 = [&]() {
    const uint64_t high = next();
    const uint64_t low = next();
    return (high << 32) | low;
  };
  constexpr u32 ops[] = {0x24, 0x28, 0x37, 0x3c, 0x44};
  for (unsigned iteration = 0; iteration < 24; ++iteration) {
    const uint64_t old = iteration == 0 ? 0 : next64();
    const uint64_t source = iteration == 1 ? 0 : next64();
    for (u32 op : ops) {
      const bool wide = op == 0x24 || op == 0x28 || op == 0x37;
      // Destination aliases source: save must not change the input to the ALU.
      AppendSMovLiteral(&code, 20, u32(source));
      AppendSMovLiteral(&code, 21, u32(source >> 32));
      AppendSMovLiteral(&code, 126, u32(old));
      AppendSMovLiteral(&code, 127, u32(old >> 32));
      code.push_back(EncodeSop1(op, 20, 20));
      code.push_back(EncodeSop1(4, 24, 126));
      code.push_back(EncodeSMovB32(26, 253));
      code.push_back(EncodeSop1(4, 126, 193));
      uint64_t result = op == 0x28                 ? source | ~old
                        : op == 0x37 || op == 0x44 ? ~source & old
                                                   : source & old;
      if (!wide)
        result = (old & 0xffffffff00000000ull) | u32(result);
      Phase4AStore(test, 20, threads, u32(old));
      Phase4AStore(test, 21, threads, u32((wide ? old : source) >> 32));
      Phase4AStore(test, 24, threads, u32(result));
      Phase4AStore(test, 25, threads, u32(result >> 32));
      Phase4AStore(test, 26, threads, wide ? result != 0 : u32(result) != 0);
    }
  }
  AppendEnd(&code);
  return test;
}

TestCase Phase4AInactiveLanes(u32 wave) {
  const u32 threads = wave;
  const u32 selected = wave == 64 ? 40 : 5;
  auto test = Phase4ASetup("Phase4AInactiveLanes", wave, threads);
  auto &code = test.code;
  AppendVMovU32(&code, 1, 100);
  code.push_back(EncodeVop2(0x25, 1, Vgpr(0), 1));
  const uint64_t mask = uint64_t{1} << selected;
  AppendSMovLiteral(&code, 126, u32(mask));
  AppendSMovLiteral(&code, 127, u32(mask >> 32));
  // All host lanes must enter this region, including the READLANE source.
  const auto branch = code.size();
  code.push_back(EncodeSopp(8, 0));
  AppendVMovU32(&code, 1, 42);
  AppendVop3(&code, 0x360, 20, Vgpr(1), InlineU32(0));
  code.push_back(EncodeVop1(2, 21, Vgpr(1)));
  AppendStoreVgprAtLaneDwordOffset(&code, 1, 0, 0);
  const auto merge = code.size();
  code[branch] = EncodeSopp(8, u32(merge - branch - 1));
  code.push_back(EncodeSop1(4, 126, 193));
  test.expected.assign(threads, 0xdeadbeef);
  test.expected[selected] = 42;
  Phase4AStore(test, 20, threads, 100);
  Phase4AStore(test, 21, threads, 42);
  AppendStoreVgprAtLaneDwordOffset(&code, 1, 0, u32(test.expected.size()));
  for (u32 lane = 0; lane < threads; ++lane)
    test.expected.push_back(lane == selected ? 42 : 100 + lane);
  // Scalar lane access is defined even with EXEC=0: READFIRSTLANE uses lane 0.
  code.push_back(EncodeSop1(4, 126, 128));
  code.push_back(EncodeVop1(2, 22, Vgpr(1)));
  code.push_back(EncodeSMovB32(23, InlineU32(11)));
  code.push_back(EncodeSop1(4, 126, 193));
  Phase4AStore(test, 22, threads, 100);
  Phase4AStore(test, 23, threads, 11);
  test.initial.assign(test.expected.size(), 0xdeadbeef);
  AppendEnd(&code);
  return test;
}

TestCase Phase4AWave32HighWords() {
  auto test = Phase4ASetup("Phase4AWave32HighWords", 32, 32);
  auto &code = test.code;
  AppendSMovLiteral(&code, 107, 0xa5a55a5a);
  AppendSMovLiteral(&code, 127, 0x12345678);
  code.push_back(EncodeVopc(0xc2, InlineU32(5), 0));
  code.push_back(EncodeSMovB32(20, 106));
  code.push_back(EncodeSMovB32(21, 107));
  code.push_back(EncodeVopc(0xd2, InlineU32(5), 0));
  code.push_back(EncodeSMovB32(22, 126));
  code.push_back(EncodeSMovB32(23, 127));
  code.push_back(EncodeSMovB32(24, 107));
  AppendSMovLiteral(&code, 126, UINT32_MAX);
  Phase4AStore(test, 20, 32, 1u << 5);
  Phase4AStore(test, 21, 32, 0xa5a55a5a);
  Phase4AStore(test, 22, 32, 1u << 5);
  Phase4AStore(test, 23, 32, 0x12345678);
  Phase4AStore(test, 24, 32, 0xa5a55a5a);
  AppendEnd(&code);
  return test;
}

void RunPhase4AContracts(VulkanHarness &vulkan) {
  unsigned count = 0;
  for (u32 wave : {32u, 64u}) {
    for (u32 threads : {1u, 4u, 32u, 48u, 64u}) {
      if (threads > wave)
        continue;
      std::printf(
          "PHASE4A_MATRIX wave=%u threads=%u host_subgroup=%u seed=0x505834a\n",
          wave, threads, vulkan.SubgroupSize());
      RunCase(&vulkan, Phase4ABranchFlags(wave, threads));
      RunCase(&vulkan, Phase4ASaveExec(wave, threads));
      count += 2;
    }
    RunCase(&vulkan, Phase4AInactiveLanes(wave));
    ++count;
  }
  RunCase(&vulkan, Phase4AWave32HighWords());
  ++count;
  std::printf("PHASE4A_PASS %u compiled, SPIR-V validated, real-device "
              "exact-readback cases\n",
              count);
}
