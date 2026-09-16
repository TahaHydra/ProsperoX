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

	// End-of-pipe submission policy: how many completions were recorded, how
	// many were batched, and what forced the submissions that did happen.
	EndOfPipePublications,
	EndOfPipeBatched,
	EndOfPipePromptSubmits, // an interrupt or flip a guest thread can wait on
	EndOfPipeLimitSubmits,  // the batch limit

	// CPU access to GPU-owned guest memory and the readbacks it forces.
	CpuReadFaults,
	CpuWriteFaults,
	ReadbackDrains, // DownloadBufferMemory batches, each one a device drain
	ReadbackBytes,

	// Release-boundary writeback and garbage collection, to tell whether either
	// is participating in the submission or readback chains.
	ReleaseWritebacks,      // RecordReleaseWriteback calls that found dirty bytes
	ReleaseWritebackCopies, // staging copies those calls queued
	GarbageCollections,

	// Invalidations that still found GPU-authored bytes and had to flush them
	// back. Every other write fault is a protection change that costs nothing,
	// so a high write-fault rate only matters if this number is large with it.
	CpuWriteFaultFlushes,

	// Guest draw and dispatch work, so per-frame CPU cost can be divided by the
	// work that caused it rather than by the frame.
	DrawCalls,
	DispatchCalls,

	// Shader program lookup outcomes, per stage per draw.
	ShaderLookups,           // GetGraphicsPrograms/GetComputeProgram stage lookups
	ShaderSourceHits,        // the program key was already known
	ShaderPermutationMisses, // ... but no permutation matched the specialization
	ShaderTranslations,      // RDNA2 -> IR translations performed
	ShaderModulesCreated,    // vkCreateShaderModule calls
	ResourceMaterializations, // MaterializeResources calls (every lookup, hit or miss)

	// Host pipeline objects.
	GraphicsPipelineLookups,
	GraphicsPipelineCreations,
	ComputePipelineCreations,

	// WAIT_REG_MEM predicates that a completion this same queue has already
	// recorded would satisfy. Resolvable is counted whether or not in-stream
	// resolution is enabled, so one run measures the opportunity and the next
	// measures the result.
	WaitResolvableInStream,
	WaitResolvedInStream,

	// Why the device was drained. Every one of these serializes the command
	// processor against the device, so knowing which is responsible decides
	// whether a remaining drain is unavoidable or a design choice.
	FinishBufferReadback,   // CPU read of GPU-owned buffer bytes
	FinishImageReadback,    // CPU read of a GPU-owned image
	FinishReleaseWriteback, // release-boundary staging ran out of budget
	FinishGdsRead,          // RELEASE_MEM sourcing its value from GDS
	FinishCompletionClock,  // a timestamp written into a cached destination
	FinishUnmap,            // guest unmapped memory the device still owns
	PredicationWaits,       // SET_PREDICATION asked the CP to wait on the device
	StreamBufferWaits,      // a stream ring wrapped onto work still in flight
	Count,
};

enum class Timer : uint32_t {
	SchedulerFinish, // time spent inside CommandScheduler::Finish
	GpuThreadParked, // time the GPU thread spent parked on an unsatisfied wait
	// The command processor blocked on presentation. This is the one stall that
	// is outside the GPU thread's control, so it separates an emulator
	// synchronization problem from a presentation or display-path one.
	FlipWait,
	// Time the GPU thread spent actually running PM4. Against GpuThreadParked
	// this is the one measurement that separates "the emulator is CPU-bound in
	// the draw path" from "the emulator is waiting for the device".
	GpuThreadRecording,

	// Decomposition of the draw recording path, in the order a draw walks it.
	// These are wall-clock and they nest: DrawShaderLookup contains
	// DrawResourceMaterialize, and all of them sit inside GpuThreadRecording.
	DrawRenderTargets,       // resolving colour/depth targets and the render state
	DrawShaderLookup,        // program cache lookup, per stage
	DrawResourceMaterialize, // ... of which, resolving the shader's resources
	DrawVertexIndex,         // vertex and index buffer acquisition
	DrawPipelineLookup,      // host pipeline key build and lookup
	DrawBindingsPrepare,     // descriptor preparation and publication
	DrawBindingsCommit,      // descriptor writes and the bind itself
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
	~ScopedTimer() noexcept { End(); }

	// Stops early, so a timer can cover less than its enclosing scope. Further
	// calls, including the destructor's, do nothing.
	void End() noexcept {
		if (!m_enabled) {
			return;
		}
		m_enabled          = false;
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
