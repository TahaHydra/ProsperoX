// Included inside VulkanHarness to reuse its real device and readback helpers.
// This changes test code only; the production scheduler and EOP paths are intact.
int ProbePhase0EopVisibility() {
    constexpr const char* name = "Phase0EopVisibility";
    EnsureRuntimeContext();
    auto& scheduler = Renderer().GetCommandScheduler();
    CommandProcessor processor(Renderer(), 0);
    processor.BufferInit();
    auto output = CreateHostBuffer(name, sizeof(u32),
        vk::BufferUsageFlagBits::eTransferDst, std::vector<u32>{0});
    auto command = scheduler.Current().Handle();
    command.fillBuffer(output.buffer, 0, sizeof(u32), 0x00505830u);
    vk::MemoryBarrier2 memory{};
    memory.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    memory.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    memory.dstStageMask = vk::PipelineStageFlagBits2::eHost;
    memory.dstAccessMask = vk::AccessFlagBits2::eHostRead;
    vk::DependencyInfo dependency{};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &memory;
    command.pipelineBarrier2(dependency);

    vk::SemaphoreTypeCreateInfo type{};
    type.semaphoreType = vk::SemaphoreType::eTimeline;
    vk::SemaphoreCreateInfo create{};
    create.pNext = &type;
    vk::Semaphore gate = nullptr;
    RequireVk(name, "create gate", m_device.createSemaphore(&create, nullptr, &gate), "createSemaphore");
    alignas(8) uint64_t label = 0; // No resource fault hides early writes.
    const auto address = reinterpret_cast<uint64_t>(&label);
    GraphicsInitJmpTables();
    const std::array<u32, 6> packet{0xc0044700u, 0x28u,
        static_cast<u32>(address), static_cast<u32>((address >> 32u) & 0xffffu) | (2u << 29u), 1u, 0u};
    Pm4Execution execution;
    const auto parsed = processor.Process(execution, packet);
    const auto label_after_parse = label;
    const auto serial = scheduler.CurrentTick();
    SubmitInfo submit;
    submit.AddWait(gate, 1);
    scheduler.Flush(submit);
    const bool retired_before_release = scheduler.IsFree(serial);
    const auto label_before_release = label;
    // Always unblock/retire before reporting an invariant mismatch.
    vk::SemaphoreSignalInfo signal{};
    signal.semaphore = gate;
    signal.value = 1;
    RequireVk(name, "release gate", m_device.signalSemaphore(&signal), "signalSemaphore");
    scheduler.Wait(serial);
    scheduler.WaitPriorityOperations(serial);
    scheduler.PopPendingOperations();
    const auto data = ReadBuffer(name, output, 1);
    const auto label_after_retirement = label;
    const bool retired_after_release = scheduler.IsFree(serial);
    DestroyBuffer(&output);
    m_device.destroySemaphore(gate, nullptr);
    std::printf("PHASE0 {\"probe\":\"eop_visibility\",\"seed\":5265456,"
        "\"label_after_parse\":%llu,\"label_before_release\":%llu,"
        "\"retired_before_release\":%s,\"retired_after_release\":%s,"
        "\"label_after_retirement\":%llu,\"gpu_readback\":%u,"
        "\"expected_label_before_release\":0,\"expected_gpu_readback\":5265456}\n",
        static_cast<unsigned long long>(label_after_parse), static_cast<unsigned long long>(label_before_release), retired_before_release ? "true" : "false",
        retired_after_release ? "true" : "false", static_cast<unsigned long long>(label_after_retirement), data[0]);
    return parsed == Pm4ProcessResult::Complete && !retired_before_release && retired_after_release && label_before_release == 0 &&
        label_after_retirement == 1 && data[0] == 0x00505830u ? 0 : 1;
}
