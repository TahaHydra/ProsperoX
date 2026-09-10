// Original synthetic packets and data. Included inside VulkanHarness.
int CheckPhase3Completion(unsigned iterations) {
  constexpr const char* name = "Phase3Completion";
  EnsureRuntimeContext();
  auto& scheduler = Renderer().GetCommandScheduler();
  CommandProcessor processor(Renderer(), 0);
  processor.BufferInit();
  GraphicsInitJmpTables();
  auto output = CreateHostBuffer(name, sizeof(u32), vk::BufferUsageFlagBits::eTransferDst,
                                 std::vector<u32>{0});
  vk::SemaphoreTypeCreateInfo type{};
  type.semaphoreType = vk::SemaphoreType::eTimeline;
  vk::SemaphoreCreateInfo create{}; create.pNext = &type;
  vk::Semaphore gate = nullptr;
  RequireVk(name, "gate", m_device.createSemaphore(&create, nullptr, &gate), "createSemaphore");
  uint32_t seed = 0x505833;
  alignas(8) uint64_t label = 0;
  const auto address = reinterpret_cast<uint64_t>(&label);
  LibKernel::EventQueue::KernelEqueue eq;
  Require(name,"event setup",LibKernel::EventQueue::KernelCreateEqueue(&eq,"release-stress")==0 &&
    Sync::AddEqEvent(Renderer(),eq,0,nullptr)==0,"event setup failed");
  for (unsigned i = 0; i < iterations; ++i) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    const uint64_t expected = (uint64_t(seed) << 32) | (i + 1);
    label = 0;
    // Block the host queue before parsing: RELEASE_MEM32 is permitted to flush.
    SubmitInfo hold; hold.AddWait(gate, i + 1);
    scheduler.Flush(hold);
    auto cmd = scheduler.Current().Handle();
    cmd.fillBuffer(output.buffer, 0, sizeof(u32), seed);
    vk::MemoryBarrier2 memory{};
    memory.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    memory.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    memory.dstStageMask = vk::PipelineStageFlagBits2::eHost;
    memory.dstAccessMask = vk::AccessFlagBits2::eHostRead;
    vk::DependencyInfo dependency{}; dependency.memoryBarrierCount=1; dependency.pMemoryBarriers=&memory;
    cmd.pipelineBarrier2(dependency);
    const bool narrow = (seed & 1) != 0;
    const bool clock = i % 19 == 0;
    const bool writeback = (seed & 2) != 0;
    const bool interrupt = (seed & 4) != 0;
    std::array<u32,8> release{KYTY_PM4(8, Pm4::IT_NOP, Pm4::R_RELEASE_MEM),
      0x28u | (writeback ? (1u << 21) : 0), ((clock ? 3u : narrow ? 1u : 2u) << 29) | (interrupt ? (2u<<24) : 0),
      u32(address), u32(address >> 32), u32(expected), u32(expected >> 32), i};
    Pm4Execution execution;
    const auto parsed = processor.Process(execution, release);
    const auto tick = scheduler.CurrentTick();
    scheduler.Flush();
    const bool early = label != 0 || scheduler.IsFree(tick);
    LibKernel::EventQueue::KernelEvent event{}; int count=0; uint32_t timeout=0;
    const auto early_event=LibKernel::EventQueue::KernelWaitEqueue(eq,&event,1,&count,&timeout);
    const bool event_early=early_event==0 || count!=0;
    const auto before_release = Sync::ReadReferenceClock();
    vk::SemaphoreSignalInfo signal{}; signal.semaphore=gate; signal.value=i+1;
    RequireVk(name, "unblock", m_device.signalSemaphore(&signal), "signalSemaphore");
    scheduler.Wait(tick); scheduler.WaitPriorityOperations(tick); scheduler.PopPendingOperations();
    const auto data = ReadBuffer(name, output, 1);
    const auto after_release = Sync::ReadReferenceClock();
    timeout=0; count=0;
    const auto event_status=LibKernel::EventQueue::KernelWaitEqueue(eq,&event,1,&count,&timeout);
    Require(name, "completion order", parsed == Pm4ProcessResult::Complete && !early && !event_early &&
      (interrupt ? event_status==0 && count==1 && event.data==i : event_status!=0 && count==0) &&
      (clock ? label >= before_release && label <= after_release :
               label == (narrow ? u32(expected) : expected)) && data[0] == seed,
      "premature label, wrong width, stale data or wrong timestamp event point");
    if (i % 1000 == 0) std::printf("PHASE3_STRESS seed=0x505833 iteration=%u state=%08x\n",i,seed);
  }
  DestroyBuffer(&output);
  Sync::DeleteEqEvent(Renderer(),eq,0); LibKernel::EventQueue::KernelDeleteEqueue(eq);
  m_device.destroySemaphore(gate,nullptr);
  std::printf("PHASE3_PASS %u controlled real-device release lifecycles\n",iterations);
  return 0;
}

int CheckPhase3Ownership() {
  constexpr const char* name = "Phase3Ownership";
  EnsureRuntimeContext();
  auto& renderer = Renderer();
  renderer.InitializeGpu(nullptr);
  auto& gpu = renderer.GetGpu();
  auto& scheduler = renderer.GetCommandScheduler();
  auto& cache = renderer.GetBufferCache();
  auto& resources = renderer.GetGpuResources();
  constexpr uint64_t size = 0x10000;
  int64_t allocation = -1;
  Require(name,"allocate", LibKernel::Memory::KernelAllocateDirectMemory(0,
    LibKernel::Memory::KernelGetDirectMemorySize(),size,size,0,&allocation)==0,"allocate failed");
  void* mapped = reinterpret_cast<void*>(0x201f00000ull);
  Require(name,"map",LibKernel::Memory::KernelMapDirectMemory(&mapped,size,3,0x10,allocation,size)==0,"map failed");
  const auto base = reinterpret_cast<uint64_t>(mapped);
  std::memset(mapped,0,size);
  resources.MapMemory(base,size);
  LibKernel::EventQueue::KernelEqueue eq;
  Require(name,"event",LibKernel::EventQueue::KernelCreateEqueue(&eq,"phase3")==0 &&
    Sync::AddEqEvent(renderer,eq,0,nullptr)==0,"event registration failed");
  CommandProcessor cp(renderer,0);
  vk::SemaphoreTypeCreateInfo type{}; type.semaphoreType=vk::SemaphoreType::eTimeline;
  vk::SemaphoreCreateInfo create{}; create.pNext=&type;
  vk::Semaphore gate=nullptr;
  RequireVk(name,"gate",m_device.createSemaphore(&create,nullptr,&gate),"createSemaphore");
  for (uint32_t i=1;i<=32;++i) {
    uint64_t old_label=0; uint32_t old_data=0;
    Require(name,"old backing",LibKernel::Memory::TryReadBacking(base,&old_data,4) &&
      LibKernel::Memory::TryReadBacking(base+8,&old_label,8),"read failed");
    uint64_t tick=0;
    gpu.SendCommandSync([&]{
      cp.BufferInit();
      SubmitInfo hold; hold.AddWait(gate,i); scheduler.Flush(hold);
      auto [buffer,offset]=cache.ObtainBuffer(base,16,true);
      buffer->Fill(offset,4,0x50580000u+i);
      const auto label_address=base+8;
      std::array<u32,8> packet{KYTY_PM4(8,Pm4::IT_NOP,Pm4::R_RELEASE_MEM),
        0x28u|(1u<<21), ((i%2 ? 1u : 2u)<<29)|(2u<<24),u32(label_address),u32(label_address>>32),i,0,i};
      Pm4Execution execution;
      Require(name,"parse",cp.Process(execution,packet)==Pm4ProcessResult::Complete,"parse failed");
      tick=scheduler.CurrentTick(); scheduler.Flush();
    });
    uint64_t early_label=0; uint32_t early_data=0;
    LibKernel::Memory::TryReadBacking(base,&early_data,4);
    LibKernel::Memory::TryReadBacking(base+8,&early_label,8);
    LibKernel::EventQueue::KernelEvent event{}; int count=0; uint32_t timeout=0;
    const auto early_event=LibKernel::EventQueue::KernelWaitEqueue(eq,&event,1,&count,&timeout);
    const bool early=early_label!=old_label || early_data!=old_data || count!=0 || early_event==0;
    vk::SemaphoreSignalInfo signal{}; signal.semaphore=gate; signal.value=i;
    RequireVk(name,"signal",m_device.signalSemaphore(&signal),"signalSemaphore");
    scheduler.Wait(tick); scheduler.WaitPriorityOperations(tick);
    uint64_t label=0;uint32_t data=0;
    LibKernel::Memory::TryReadBacking(base,&data,4);
    LibKernel::Memory::TryReadBacking(base+8,&label,8);
    timeout=0;count=0;
    const auto status=LibKernel::EventQueue::KernelWaitEqueue(eq,&event,1,&count,&timeout);
    Require(name,"data-label-event",!early && data==0x50580000u+i && label==i &&
      status==0 && count==1 && event.data==i,"early/stale publication or missing interrupt");
    gpu.SendCommandSync([&]{
      uint32_t acquired=0;
      cache.ReadCommandMemory(base,&acquired,4);
      uint64_t cached_label=0;cache.ReadCommandMemory(base+8,&cached_label,8);
      Require(name,"CPU argument acquire",acquired==data && cached_label==label,
        "argument acquisition or later readback restored stale bytes");
    });
  }
  gpu.SendCommandSync([&]{
    // CPU backing contains poison; arguments are produced exclusively by GPU
    // transfers. Observe the renderer boundary without requiring Phase 4 shaders.
    const std::array<u32,8> poison{99,99,99,99,99,99,99,99};
    LibKernel::Memory::WriteBacking(base+0x100,poison.data(),sizeof(poison));
    cache.InvalidateMemory(base+0x100,sizeof(poison));
    auto [args,offset]=cache.ObtainBuffer(base+0x100,sizeof(poison),true);
    args->Fill(offset,12,7);
    cp.SetDispatchIndirectArgsBaseAddress(base+0x100);
    cp.DispatchIndirect(0,1);
    auto observed=RenderExecutorTestAccess::CommandArguments(scheduler.Current());
    Require(name,"GPU indirect dimensions",observed[0]==u32(CommandBufferDebugOp::DispatchDirect) &&
      observed[1]==7 && observed[2]==7 && observed[3]==7,"dispatch used stale dimensions");
    auto [count,count_offset]=cache.ObtainBuffer(base+0x110,4,true);
    count->Fill(count_offset,4,0);
    cp.SetDrawIndirectArgsBaseAddress(base+0x100);
    cp.DrawIndirectMulti(0,99,reinterpret_cast<uint32_t*>(base+0x110),16,2,false);
    Require(name,"GPU indirect count",RenderExecutorTestAccess::CommandArguments(scheduler.Current())==observed,
      "zero GPU draw count failed to suppress draw");
    auto [draw_args,draw_offset]=cache.ObtainBuffer(base+0x100,16,true);
    draw_args->Fill(draw_offset,16,7);
    auto [draw_count,draw_count_offset]=cache.ObtainBuffer(base+0x110,4,true);
    draw_count->Fill(draw_count_offset,4,1);
    cp.DrawIndirectMulti(0,2,reinterpret_cast<uint32_t*>(base+0x110),16,2,false);
    observed=RenderExecutorTestAccess::CommandArguments(scheduler.Current());
    Require(name,"GPU draw arguments",observed[0]==u32(CommandBufferDebugOp::DrawIndexAuto) &&
      observed[1]==7 && observed[3]==7 && observed[4]==7,"draw used stale arguments");
    auto [predicate,predicate_offset]=cache.ObtainBuffer(base+0x118,8,true);
    predicate->Fill(predicate_offset,8,0);
    cp.SetPredication(1,3,0,reinterpret_cast<void*>(base+0x118),2);
    Require(name,"GPU predicate zero",cp.ShouldSkipPredicatedPackets(),"stale nonzero predicate");
    auto [predicate2,predicate_offset2]=cache.ObtainBuffer(base+0x118,8,true);
    predicate2->Fill(predicate_offset2,8,1);
    cp.SetPredication(1,3,0,reinterpret_cast<void*>(base+0x118),2);
    Require(name,"GPU predicate nonzero",!cp.ShouldSkipPredicatedPackets(),"stale zero predicate");
    const auto before=Sync::ReadReferenceClock();
    Sync::WriteCompletionClock(scheduler.Current(),reinterpret_cast<uint64_t*>(base+8),true,false,0,0);
    const auto tick=scheduler.CurrentTick();scheduler.Finish();scheduler.WaitPriorityOperations(tick);
    uint64_t timestamp=0;cache.ReadCommandMemory(base+8,&timestamp,8);
    Require(name,"cached clock",timestamp>=before && timestamp<=Sync::ReadReferenceClock(),
      "cached timestamp lost its retirement value");
    auto [bulk,bulk_offset]=cache.ObtainBuffer(base,size,true);
    bulk->Fill(bulk_offset,size,0x13572468);
    const auto budget_tick=scheduler.CurrentTick();
    // 1025 x 64 KiB forces the 64 MiB pending-staging backpressure path.
    for (unsigned i=0;i<1025;++i) cache.RecordReleaseWriteback();
    Require(name,"bounded staging",scheduler.CurrentTick()>budget_tick,
      "writeback staging failed to apply its pending-memory budget");
    const auto last_tick=scheduler.CurrentTick();scheduler.Finish();scheduler.WaitPriorityOperations(last_tick);
    uint32_t first=0,last=0;
    LibKernel::Memory::TryReadBacking(base,&first,4);
    LibKernel::Memory::TryReadBacking(base+size-4,&last,4);
    Require(name,"staging pressure readback",first==0x13572468 && last==first,
      "writeback staging reused data before retirement");
  });
  resources.UnmapMemory(base,size);
  Require(name,"unmap",LibKernel::Memory::KernelMunmap(base,size)==0 &&
    LibKernel::Memory::KernelReleaseDirectMemory(allocation,size)==0,"unmap failed");
  Sync::DeleteEqEvent(renderer,eq,0);LibKernel::EventQueue::KernelDeleteEqueue(eq);
  m_device.destroySemaphore(gate,nullptr);

  // A permanently unsatisfied wait must retain the continuation until canceled.
  alignas(8) uint64_t label=0; uint32_t suffix=0;
  const auto address=reinterpret_cast<uint64_t>(&label);
  const auto dst=reinterpret_cast<uint64_t>(&suffix);
  std::array<u32,12> wait{KYTY_PM4(7,Pm4::IT_WAIT_REG_MEM,0),0x13,u32(address),u32(address>>32),
    1,UINT32_MAX,0,KYTY_PM4(5,Pm4::IT_WRITE_DATA,0),0,u32(dst),u32(dst>>32),42};
  const auto nested=reinterpret_cast<uint64_t>(wait.data());
  std::array<u32,4> outer{KYTY_PM4(4,Pm4::IT_INDIRECT_BUFFER,0),u32(nested),u32(nested>>32),0x0f200000u|u32(wait.size())};
  gpu.Submit(outer,{});
  for(unsigned i=0;i<100;++i) gpu.SendCommandSync([&]{Require(name,"no producer",suffix==0 && label==0,"wait fabricated progress");});
  std::array<u32,5> producer{KYTY_PM4(5,Pm4::IT_WRITE_DATA,0),0,u32(address),u32(address>>32),1};
  gpu.SubmitCompute(0x20,producer);
  gpu.Done();
  Require(name,"cross queue nested resume",label==1 && suffix==42,"nested continuation did not resume");
  label=0;suffix=0;
  std::array<u32,14> wait64{KYTY_PM4(9,Pm4::IT_WAIT_REG_MEM_64,0),0x13,u32(address),u32(address>>32),
    1,0xabcd,0xff,UINT32_MAX,0,KYTY_PM4(5,Pm4::IT_WRITE_DATA,0),0,u32(dst),u32(dst>>32),43};
  gpu.Submit(wait64,{});
  gpu.SendCommandSync([&]{Require(name,"64-bit unsatisfied",suffix==0,"64-bit wait ignored upper mask");});
  std::array<u32,6> producer64{KYTY_PM4(6,Pm4::IT_WRITE_DATA,0),0,u32(address),u32(address>>32),0xffff0001u,0xabcdu};
  gpu.SubmitCompute(0x21,producer64);gpu.Done();
  Require(name,"masked 64-bit acquire",suffix==43,"masked 64-bit wait failed");
  label=0;suffix=0;
  gpu.Submit(outer,{});
  gpu.SendCommandSync([&]{Require(name,"still unsatisfied",label==0 && suffix==0,"unexpected progress");});
  gpu.Shutdown();
  Require(name,"cancel",suffix==0 && label==0 && gpu.CanceledSubmissions()==1,
    "shutdown lost cancellation or published completion");
  renderer.ShutdownGpu();
  std::puts("PHASE3_PASS backing visibility, event order, cached label reuse, command acquire, cancel/drain");
  return 0;
}

int CheckPhase3FlipBoundary() {
  constexpr const char* name="Phase3FlipBoundary";
  EnsureRuntimeContext();
  auto& scheduler=Renderer().GetCommandScheduler();
  CommandProcessor cp(Renderer(),0);cp.BufferInit();
  vk::SemaphoreTypeCreateInfo type{};type.semaphoreType=vk::SemaphoreType::eTimeline;
  vk::SemaphoreCreateInfo create{};create.pNext=&type;vk::Semaphore gate=nullptr;
  RequireVk(name,"gate",m_device.createSemaphore(&create,nullptr,&gate),"createSemaphore");
  alignas(8) uint32_t label=0;
  bool ready=false, ordered=false;
  Sync::CompleteFlipAtEndOfPipe(scheduler.Current(),&label,7,[&]{ordered=label==7;ready=true;});
  const auto tick=scheduler.CurrentTick();SubmitInfo hold;hold.AddWait(gate,1);scheduler.Flush(hold);
  const bool early=ready || label!=0 || scheduler.IsFree(tick);
  vk::SemaphoreSignalInfo signal{};signal.semaphore=gate;signal.value=1;
  RequireVk(name,"signal",m_device.signalSemaphore(&signal),"signalSemaphore");
  scheduler.Wait(tick);scheduler.WaitPriorityOperations(tick);
  Require(name,"flip publication",!early && ready && ordered,"flip became ready before retirement or its label");
  m_device.destroySemaphore(gate,nullptr);
  std::puts("PHASE3_PASS shared CPU/GPU flip completion boundary (presentation surface excluded)");
  return 0;
}

int RejectPhase3Packet(const char* variant) {
  EnsureRuntimeContext();
  CommandProcessor cp(Renderer(),0);cp.BufferInit();GraphicsInitJmpTables();
  // Place the supplied packet at the very end of an accessible host page.
  SYSTEM_INFO info{};GetSystemInfo(&info);const size_t page=info.dwPageSize;
  auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,2*page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  Require("Phase3PacketBounds","allocate",memory!=nullptr,"VirtualAlloc failed");
  DWORD previous=0;Require("Phase3PacketBounds","guard",VirtualProtect(memory+page,page,PAGE_NOACCESS,&previous)!=0,"VirtualProtect failed");
  auto* packet=reinterpret_cast<u32*>(memory+page-8);
  packet[0]=0xc0044700u;packet[1]=0;
  if(std::strcmp(variant,"--phase3-short-release")==0)packet[0]=KYTY_PM4(8,Pm4::IT_NOP,Pm4::R_RELEASE_MEM);
  if(std::strcmp(variant,"--phase3-short-wait")==0)packet[0]=KYTY_PM4(9,Pm4::IT_WAIT_REG_MEM_64,0);
  Pm4Execution execution;cp.Process(execution,{packet,2});
  return 0; // The parent requires the structured exit, never an access violation.
}
