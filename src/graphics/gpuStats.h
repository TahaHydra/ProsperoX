#ifndef EMULATOR_SRC_GRAPHICS_GPUSTATS_H_
#define EMULATOR_SRC_GRAPHICS_GPUSTATS_H_

#include <atomic>
#include <chrono>
#include <cstdint>

namespace Libs::Graphics::Stats {

// Counters for the guest-GPU execution pipeline. They exist to make the
// submission/wait behaviour of a running title measurable without a profiler
// build: the ratio between guest submissions, blocked slices, device drains and
// queue submissions is what distinguishes a genuine GPU bottleneck from
// emulator-side synchronization overhead.
//
// Reporting is opt-in through the KYTY_GPU_STATS environment variable and emits
// one summary line per second. When it is off every entry point below is a
// predictable-branch early return.
enum class Counter : uint32_t {
	GuestSubmissions,   // guest command buffers handed to the GPU thread
	ProcessAttempts,    // GuestGpu::Process invocations
	BlockedAttempts,    // ... that suspended on an unsatisfied wait
	WaitRegMemFailures, // WAIT_REG_MEM predicates that evaluated false
	WaitDrains,         // device drains taken to re-read a wait label
	QueueSubmits,       // vkQueueSubmit calls
	ElidedSubmits,      // flushes skipped because nothing was recorded
	SchedulerFinishes,  // CommandScheduler::Finish calls
	GpuThreadWakeups,   // parks that ended on a publication rather than a timeout
	GpuThreadTimeouts,  // parks that ended on the safety-net timeout
	Count,
};

enum class Timer : uint32_t {
	SchedulerFinish, // time spent inside CommandScheduler::Finish
	GpuThreadParked, // time the GPU thread spent parked on an unsatisfied wait
	Count,
};

[[nodiscard]] bool Enabled() noexcept;

void Add(Counter counter, uint64_t value = 1) noexcept;
void AddTime(Timer timer, uint64_t nanoseconds) noexcept;

// Emits a summary line at most once per second and resets the window. Cheap
// enough to call from the GPU thread's main loop.
void Report() noexcept;

class ScopedTimer final {
public:
	explicit ScopedTimer(Timer timer) noexcept: m_timer(timer), m_enabled(Enabled()) {
		if (m_enabled) {
			m_start = std::chrono::steady_clock::now();
		}
	}
	~ScopedTimer() noexcept {
		if (!m_enabled) {
			return;
		}
		const auto elapsed = std::chrono::steady_clock::now() - m_start;
		AddTime(m_timer, static_cast<uint64_t>(
		                     std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
	}

	ScopedTimer(const ScopedTimer&)            = delete;
	ScopedTimer& operator=(const ScopedTimer&) = delete;
	ScopedTimer(ScopedTimer&&)                 = delete;
	ScopedTimer& operator=(ScopedTimer&&)      = delete;

private:
	Timer                                 m_timer;
	bool                                  m_enabled;
	std::chrono::steady_clock::time_point m_start;
};

} // namespace Libs::Graphics::Stats

#endif // EMULATOR_SRC_GRAPHICS_GPUSTATS_H_
