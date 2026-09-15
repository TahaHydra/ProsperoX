#ifndef KYTY_COMMON_PERF_TRACE_H_
#define KYTY_COMMON_PERF_TRACE_H_

#include "common/timer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Common::PerfTrace {

// Opt-in diagnostic wall-time counters, not GPU timestamps or a benchmark.
// Buckets may nest and overlap across threads; their times must not be summed.
// Atomic counter exchanges produce approximate windows, not coherent snapshots.
enum class Bucket : uint32_t {
	FlipSubmitSlotWait,
	FlipCompletionWait,
	VideoOutPrepareConfigMutexWait,
	PresentThreadSleep,
	PresentThreadWork,
	FlipPresenterPresent,
	PresenterPrepareFrame,
	PresenterResolveSurface,
	PresenterPresentTotal,
	FramePoolAvailableWait,
	FramePoolGpuWait,
	RendererMutexWait,
	SwapchainAcquireTickWait,
	VkAcquireNextImage,
	PresentFenceWait,
	VkQueuePresent,
	VkQueueSubmit,
	WindowUpdateTitle,
	SchedulerWait,
	SchedulerFinish,
	SchedulerFlushAndWait,
	Count,
};

enum class Event : uint32_t {
	PresentLoop,
	FlipNotReady,
	FlipNotDue,
	FlipQueueFull,
	GuestFlip,
	Count,
};

inline constexpr size_t BucketCount = static_cast<size_t>(Bucket::Count);
inline constexpr size_t EventCount  = static_cast<size_t>(Event::Count);

struct BucketState {
	std::atomic<uint64_t> ticks {0};
	std::atomic<uint64_t> calls {0};
};

struct EventState {
	std::atomic<uint64_t> value {0};
};

inline std::array<BucketState, BucketCount> g_buckets {};
inline std::array<EventState, EventCount>   g_events {};
inline std::atomic<uint64_t>                g_last_report_tick {0};

[[nodiscard]] inline bool Enabled() noexcept {
	static const bool enabled = [] {
		const char* value = std::getenv("PROSPEROX_PERF_TRACE");
		return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
	}();
	return enabled;
}

[[nodiscard]] inline uint64_t Now() noexcept {
	return Enabled() ? Timer::QueryPerformanceCounter() : 0;
}

inline void AddTicks(Bucket bucket, uint64_t ticks) noexcept {
	if (!Enabled()) {
		return;
	}

	auto& state = g_buckets[static_cast<size_t>(bucket)];
	state.ticks.fetch_add(ticks, std::memory_order_relaxed);
	state.calls.fetch_add(1, std::memory_order_relaxed);
}

inline void AddElapsed(Bucket bucket, uint64_t begin) noexcept {
	if (begin == 0 || !Enabled()) {
		return;
	}

	const auto end = Timer::QueryPerformanceCounter();
	if (end >= begin) {
		AddTicks(bucket, end - begin);
	}
}

inline void Count(Event event) noexcept {
	if (!Enabled()) {
		return;
	}

	g_events[static_cast<size_t>(event)].value.fetch_add(1, std::memory_order_relaxed);
}

class Scope final {
public:
	explicit Scope(Bucket bucket) noexcept: m_bucket(bucket), m_begin(Now()) {}

	~Scope() { AddElapsed(m_bucket, m_begin); }

	Scope(const Scope&)            = delete;
	Scope& operator=(const Scope&) = delete;

private:
	Bucket   m_bucket;
	uint64_t m_begin;
};

inline void ReportIfDue() noexcept {
	if (!Enabled()) {
		return;
	}

	const auto frequency = Timer::QueryPerformanceFrequency();
	if (frequency == 0) {
		return;
	}

	const auto now = Timer::QueryPerformanceCounter();

	auto previous = g_last_report_tick.load(std::memory_order_relaxed);
	if (previous == 0) {
		g_last_report_tick.compare_exchange_strong(previous, now, std::memory_order_relaxed,
		                                           std::memory_order_relaxed);
		return;
	}

	if (now - previous < frequency) {
		return;
	}

	if (!g_last_report_tick.compare_exchange_strong(previous, now, std::memory_order_relaxed,
	                                                std::memory_order_relaxed)) {
		return;
	}

	const double seconds = static_cast<double>(now - previous) / static_cast<double>(frequency);

	const auto present_loops = g_events[static_cast<size_t>(Event::PresentLoop)].value.exchange(
	    0, std::memory_order_relaxed);
	const auto not_ready = g_events[static_cast<size_t>(Event::FlipNotReady)].value.exchange(
	    0, std::memory_order_relaxed);
	const auto not_due = g_events[static_cast<size_t>(Event::FlipNotDue)].value.exchange(
	    0, std::memory_order_relaxed);
	const auto queue_full = g_events[static_cast<size_t>(Event::FlipQueueFull)].value.exchange(
	    0, std::memory_order_relaxed);
	const auto guest_flips = g_events[static_cast<size_t>(Event::GuestFlip)].value.exchange(
	    0, std::memory_order_relaxed);

	const double fps = seconds > 0.0 ? static_cast<double>(guest_flips) / seconds : 0.0;

	std::printf("PERF fps=%6.2f loops=%6.1f/s not_ready=%6.1f/s "
	            "not_due=%6.1f/s queue_full=%6.1f/s window=%.3fs\n",
	            fps, seconds > 0.0 ? static_cast<double>(present_loops) / seconds : 0.0,
	            seconds > 0.0 ? static_cast<double>(not_ready) / seconds : 0.0,
	            seconds > 0.0 ? static_cast<double>(not_due) / seconds : 0.0,
	            seconds > 0.0 ? static_cast<double>(queue_full) / seconds : 0.0, seconds);

	static constexpr std::array<const char*, BucketCount> names = {
	    "flip_submit_slot_wait",
	    "flip_completion_wait",
	    "videoout_prepare_cfg_mutex",
	    "present_thread_sleep",
	    "present_thread_work",
	    "flip_presenter_present",
	    "presenter_prepare_frame",
	    "presenter_resolve_surface",
	    "presenter_present_total",
	    "frame_pool_available_wait",
	    "frame_pool_gpu_wait",
	    "renderer_mutex_wait",
	    "swapchain_acquire_tick_wait",
	    "vk_acquire_next_image",
	    "present_fence_wait",
	    "vk_queue_present",
	    "vk_queue_submit",
	    "window_update_title",
	    "scheduler_wait",
	    "scheduler_finish",
	    "scheduler_flush_and_wait",
	};

	for (size_t i = 0; i < BucketCount; ++i) {
		auto& state = g_buckets[i];

		const auto ticks = state.ticks.exchange(0, std::memory_order_relaxed);
		const auto calls = state.calls.exchange(0, std::memory_order_relaxed);

		if (ticks == 0 && calls == 0) {
			continue;
		}

		const double total_ms =
		    static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency);

		const double ms_per_second = seconds > 0.0 ? total_ms / seconds : 0.0;

		const double average_ms = calls != 0 ? total_ms / static_cast<double>(calls) : 0.0;

		std::printf("PERF %-30s time=%9.3f ms/s calls=%6llu avg=%8.3f ms\n", names[i],
		            ms_per_second, static_cast<unsigned long long>(calls), average_ms);
	}

	std::fflush(stdout);
}

} // namespace Common::PerfTrace

#endif // KYTY_COMMON_PERF_TRACE_H_
