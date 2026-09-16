#include "graphics/gpuStats.h"

#include "common/logging/log.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace Libs::Graphics::Stats {

namespace {

constexpr auto CounterCount = static_cast<uint32_t>(Counter::Count);
constexpr auto TimerCount   = static_cast<uint32_t>(Timer::Count);

std::array<std::atomic<uint64_t>, CounterCount> g_counters {};
std::array<std::atomic<uint64_t>, TimerCount>   g_timers {};

std::mutex                            g_report_mutex;
std::chrono::steady_clock::time_point g_window_start;
bool                                  g_window_started = false;

} // namespace

bool Enabled() noexcept {
	static const bool enabled = std::getenv("KYTY_GPU_STATS") != nullptr;
	return enabled;
}

void Add(Counter counter, uint64_t value) noexcept {
	if (!Enabled()) {
		return;
	}
	g_counters[static_cast<uint32_t>(counter)].fetch_add(value, std::memory_order_relaxed);
}

void AddTime(Timer timer, uint64_t nanoseconds) noexcept {
	if (!Enabled()) {
		return;
	}
	g_timers[static_cast<uint32_t>(timer)].fetch_add(nanoseconds, std::memory_order_relaxed);
}

void Report() noexcept {
	if (!Enabled()) {
		return;
	}
	const auto now = std::chrono::steady_clock::now();

	std::array<uint64_t, CounterCount> counters {};
	std::array<uint64_t, TimerCount>   timers {};
	double                             seconds = 0.0;
	{
		std::lock_guard lock(g_report_mutex);
		if (!g_window_started) {
			g_window_start   = now;
			g_window_started = true;
			return;
		}
		const auto elapsed = now - g_window_start;
		if (elapsed < std::chrono::seconds(1)) {
			return;
		}
		g_window_start = now;
		seconds        = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
		for (uint32_t i = 0; i < CounterCount; i++) {
			counters[i] = g_counters[i].exchange(0, std::memory_order_relaxed);
		}
		for (uint32_t i = 0; i < TimerCount; i++) {
			timers[i] = g_timers[i].exchange(0, std::memory_order_relaxed);
		}
	}

	const auto per_second = [seconds](uint64_t value) { return static_cast<double>(value) / seconds; };
	const auto millis     = [seconds](uint64_t nanoseconds) {
        return static_cast<double>(nanoseconds) / 1e6 / seconds;
	};

	const auto count = [&counters](Counter counter) {
		return counters[static_cast<uint32_t>(counter)];
	};
	const auto time = [&timers](Timer timer) { return timers[static_cast<uint32_t>(timer)]; };

	char line[1536];
	std::snprintf(
	    line, sizeof(line),
	    "gpu stats (per second):\n"
	    "  pm4      submit=%.1f process=%.1f blocked=%.1f waitfail=%.1f drains=%.1f\n"
	    "  waits    instream_possible=%.1f instream_resolved=%.1f\n"
	    "  submit   vksubmit=%.1f elided=%.1f finish=%.1f finish_ms=%.1f\n"
	    "  eop      publish=%.1f batched=%.1f prompt=%.1f limit=%.1f\n"
	    "  readback readfault=%.1f writefault=%.1f flushed=%.1f drains=%.1f MiB=%.2f\n"
	    "  work     draws=%.1f dispatches=%.1f\n"
	    "  shaders  lookups=%.1f srchit=%.1f permmiss=%.1f translate=%.1f module=%.1f "
	    "material=%.1f\n"
	    "  pipeline lookups=%.1f gfxnew=%.1f csnew=%.1f\n"
	    "  upkeep   writeback=%.1f copies=%.1f gc=%.1f\n"
	    "  time     recording_ms=%.1f parked_ms=%.1f flipwait_ms=%.1f wake=%.1f timeout=%.1f\n",
	    per_second(count(Counter::GuestSubmissions)), per_second(count(Counter::ProcessAttempts)),
	    per_second(count(Counter::BlockedAttempts)), per_second(count(Counter::WaitRegMemFailures)),
	    per_second(count(Counter::WaitDrains)),
	    per_second(count(Counter::WaitResolvableInStream)),
	    per_second(count(Counter::WaitResolvedInStream)), per_second(count(Counter::QueueSubmits)),
	    per_second(count(Counter::ElidedSubmits)), per_second(count(Counter::SchedulerFinishes)),
	    millis(time(Timer::SchedulerFinish)), per_second(count(Counter::EndOfPipePublications)),
	    per_second(count(Counter::EndOfPipeBatched)),
	    per_second(count(Counter::EndOfPipePromptSubmits)),
	    per_second(count(Counter::EndOfPipeLimitSubmits)), per_second(count(Counter::CpuReadFaults)),
	    per_second(count(Counter::CpuWriteFaults)), per_second(count(Counter::CpuWriteFaultFlushes)),
	    per_second(count(Counter::ReadbackDrains)),
	    per_second(count(Counter::ReadbackBytes)) / (1024.0 * 1024.0),
	    per_second(count(Counter::DrawCalls)), per_second(count(Counter::DispatchCalls)),
	    per_second(count(Counter::ShaderLookups)), per_second(count(Counter::ShaderSourceHits)),
	    per_second(count(Counter::ShaderPermutationMisses)),
	    per_second(count(Counter::ShaderTranslations)),
	    per_second(count(Counter::ShaderModulesCreated)),
	    per_second(count(Counter::ResourceMaterializations)),
	    per_second(count(Counter::GraphicsPipelineLookups)),
	    per_second(count(Counter::GraphicsPipelineCreations)),
	    per_second(count(Counter::ComputePipelineCreations)),
	    per_second(count(Counter::ReleaseWritebacks)),
	    per_second(count(Counter::ReleaseWritebackCopies)),
	    per_second(count(Counter::GarbageCollections)), millis(time(Timer::GpuThreadRecording)),
	    millis(time(Timer::GpuThreadParked)), millis(time(Timer::FlipWait)),
	    per_second(count(Counter::GpuThreadWakeups)), per_second(count(Counter::GpuThreadTimeouts)));

	LOGF("%s", line);
	std::fputs(line, stdout);
	std::fflush(stdout);
}

} // namespace Libs::Graphics::Stats
