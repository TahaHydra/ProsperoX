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

	char line[512];
	std::snprintf(line, sizeof(line),
	              "gpu stats (per second): submit=%.1f process=%.1f blocked=%.1f waitfail=%.1f "
	              "drains=%.1f vksubmit=%.1f elided=%.1f finish=%.1f finish_ms=%.1f "
	              "parked_ms=%.1f wake=%.1f timeout=%.1f\n",
	              per_second(counters[static_cast<uint32_t>(Counter::GuestSubmissions)]),
	              per_second(counters[static_cast<uint32_t>(Counter::ProcessAttempts)]),
	              per_second(counters[static_cast<uint32_t>(Counter::BlockedAttempts)]),
	              per_second(counters[static_cast<uint32_t>(Counter::WaitRegMemFailures)]),
	              per_second(counters[static_cast<uint32_t>(Counter::WaitDrains)]),
	              per_second(counters[static_cast<uint32_t>(Counter::QueueSubmits)]),
	              per_second(counters[static_cast<uint32_t>(Counter::ElidedSubmits)]),
	              per_second(counters[static_cast<uint32_t>(Counter::SchedulerFinishes)]),
	              millis(timers[static_cast<uint32_t>(Timer::SchedulerFinish)]),
	              millis(timers[static_cast<uint32_t>(Timer::GpuThreadParked)]),
	              per_second(counters[static_cast<uint32_t>(Counter::GpuThreadWakeups)]),
	              per_second(counters[static_cast<uint32_t>(Counter::GpuThreadTimeouts)]));

	LOGF("%s", line);
	std::fputs(line, stdout);
	std::fflush(stdout);
}

} // namespace Libs::Graphics::Stats
