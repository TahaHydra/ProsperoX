#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_GRAPHICSRUN_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_GRAPHICSRUN_H_

#include "common/abi.h"
#include "common/common.h"
#include "common/threads.h"
#include "common/uniqueFunction.h"
#include "graphics/guest_gpu/command_processor/commandProcessor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace Libs::Graphics {

class RenderContext;

class GuestGpu final {
public:
	explicit GuestGpu(RenderContext& renderer);
	~GuestGpu();
	KYTY_CLASS_NO_COPY(GuestGpu);

	void               Shutdown();
	[[nodiscard]] bool IsStopping();
	[[nodiscard]] uint64_t CanceledSubmissions() const noexcept { return m_canceled_submissions.load(); }
	void               SendCommand(Common::UniqueFunction<void>&& command);
	void               SendCommandSync(Common::UniqueFunction<void>&& command);
	// Republishes the wait state: a command processor parked on a memory
	// predicate re-evaluates it instead of waiting out its safety-net timeout.
	void               NotifyGuestMemoryPublished();

	// Submitted command memory is borrowed and must remain valid until GPU execution completes.
	void              Submit(std::span<const uint32_t> draw_commands,
	                         std::span<const uint32_t> constant_commands);
	void              SubmitCompute(uint32_t queue, std::span<const uint32_t> commands);
	void              SubmitFlipPreparation(uint64_t request_id);
	void              Done();
	[[nodiscard]] int GetFrameNum() const;

	[[nodiscard]] static bool IsGpuThread() noexcept;

private:
	static constexpr uint32_t ComputePipeCount     = 7;
	static constexpr uint32_t QueuesPerComputePipe = 8;
	static constexpr uint32_t ComputeQueueCount    = ComputePipeCount * QueuesPerComputePipe;
	static constexpr uint32_t ComputeQueueBase     = 0x20;
	static constexpr uint32_t QueueCount           = 1 + ComputeQueueCount;

	enum class SubmissionType { Graphics, Compute, FlipPreparation };

	struct Submission {
		SubmissionType            type     = SubmissionType::Graphics;
		uint32_t                  queue_id = 0;
		std::span<const uint32_t> commands;
		std::span<const uint32_t> constant_commands;
		Pm4Execution              command_execution;
		Pm4Execution              constant_execution;
		bool                      reset_processor   = false;
		bool                      started           = false;
		bool                      command_complete  = false;
		bool                      constant_complete = false;
		bool                      blocked           = false;
		// Publication counter observed before the wait predicate was last
		// evaluated. Any later publication can have satisfied it, so the
		// submission becomes runnable again without waiting for a timeout.
		uint64_t                  wake_generation   = 0;
		// Wake-up hint for the predicate the slice suspended on, if any.
		Pm4WaitCondition          wait;
		uint64_t                  flip_request_id   = 0;
	};

	// Safety net for a wait only a guest CPU thread can satisfy. Everything the
	// emulator itself publishes wakes the thread directly, so this interval is
	// never on the critical path of a GPU-produced value.
	static constexpr uint32_t ParkTimeoutUs         = 1000;
	// Bound on the device-timeline wait taken when no completion callback is
	// queued to do the waiting for us. Short enough that a guest submission
	// arriving meanwhile does not queue up behind it.
	static constexpr uint64_t ParkTimelineTimeoutNs = 250000;
	static constexpr uint32_t ParkSpinIterations    = 8;

	void              Enqueue(Submission submission);
	void              WaitForIdle();
	void              ProcessCommands();
	bool              Process(Submission& submission);
	bool              Park(uint64_t generation, std::span<const Pm4WaitCondition> conditions);
	[[nodiscard]] bool Published(uint64_t generation) const noexcept {
		return m_wake_generation.load(std::memory_order_acquire) != generation;
	}
	static void       ThreadRun(void* data);
	CommandProcessor& GetProcessor(uint32_t queue_id);

	RenderContext&                                 m_renderer;
	Common::Mutex                                  m_submission_mutex;
	Common::Mutex                                  m_queue_mutex;
	std::mutex                                     m_shutdown_mutex;
	Common::CondVar                                m_work_available;
	Common::CondVar                                m_idle;
	std::array<std::deque<Submission>, QueueCount> m_queues;
	std::deque<Common::UniqueFunction<void>>       m_commands;
	std::atomic_uint32_t                           m_pending_commands {0};
	uint32_t                                       m_next_queue        = 0;
	uint32_t                                       m_submission_count  = 0;
	// Incremented under m_queue_mutex whenever the emulator publishes something
	// a parked wait could observe; readable without the lock so a parked thread
	// can leave its spin early.
	std::atomic<uint64_t>                          m_wake_generation {0};
	// Reused across parks; owned by the GPU thread.
	std::vector<Pm4WaitCondition>                  m_park_conditions;
	bool                                           m_processing        = false;
	bool                                           m_graphics_done     = true;
	bool                                           m_accepting         = true;
	bool                                           m_stopping          = false;
	bool                                           m_shutdown_complete = false;

	std::unique_ptr<CommandProcessor>                                m_gfx_cp;
	std::array<std::unique_ptr<CommandProcessor>, ComputeQueueCount> m_compute_cp;

	uint64_t        m_submit_id = 0;
	std::atomic<uint64_t> m_canceled_submissions{0};
	std::atomic_int m_done_num  = 0;
	std::jthread    m_thread;

	friend class CommandProcessor;
};
} // namespace Libs::Graphics

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_GRAPHICSRUN_H_ */
