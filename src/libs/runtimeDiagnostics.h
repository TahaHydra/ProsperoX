#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

namespace Libs::RuntimeDiagnostics {

using HleCallToken = uint64_t;

class State final {
public:
	explicit State(size_t recent_capacity = 128);

	HleCallToken EnterHle(uint32_t thread_id, const char* library, const char* module, const char* function,
	                      uint64_t caller_address, uint64_t now_us);
	void ExitHle(HleCallToken token, uint64_t now_us);

	void RecordGpuDispatch(uint64_t submission_id, uint64_t shader_address, uint32_t groups_x, uint32_t groups_y,
	                       uint32_t groups_z, uint64_t now_us);
	void RecordGpuDraw(uint64_t submission_id, const char* kind, uint64_t now_us);

	void RecordVideoSubmit(bool gpu_submit, int buffer_index, uint64_t now_us);
	void RecordVideoPrepared(uint64_t now_us);
	void RecordVideoReady(uint64_t now_us);
	void RecordVideoPresented(int buffer_index, uint64_t now_us);

	void RecordShaderPhase(const char* stage, uint64_t shader_hash, const char* phase, uint64_t now_us);

	// Guest thread lifecycle. A thread that is silent is only evidence once it
	// is known to still exist: without this a stalled main thread and an exited
	// one look identical in the report.
	void RecordGuestThreadStart(uint32_t thread_id, const char* name, uint64_t entry_address,
	                            uint64_t host_thread_id, uint64_t now_us);
	void RecordGuestThreadExit(uint32_t thread_id, uint64_t now_us);

	[[nodiscard]] std::string BuildReport(uint64_t now_us) const;
	[[nodiscard]] size_t RecentEventCount() const;

	// Full, bounded capture of the HLE boundary. The recent-event ring is a
	// window, and a poll loop rolls it over in under a second -- the events
	// that explain how a title reached its loop are gone before anyone looks.
	// Opt in with PROSPEROX_RUNTIME_DIAG_TRACE=<count>; capture stops at the
	// cap rather than growing without bound.
	void SetTraceLimit(size_t limit);
	[[nodiscard]] std::vector<std::string> TakeTracedEvents();

private:
	struct HleCall {
		uint32_t thread_id = 0;
		std::string library;
		std::string module;
		std::string function;
		uint64_t caller_address = 0;
		uint64_t entered_us = 0;
	};

	struct LastDispatch {
		uint64_t submission_id = 0;
		uint64_t shader_address = 0;
		uint32_t groups_x = 0;
		uint32_t groups_y = 0;
		uint32_t groups_z = 0;
		uint64_t time_us = 0;
		bool valid = false;
	};

	struct LastDraw {
		uint64_t submission_id = 0;
		std::string kind;
		uint64_t time_us = 0;
		bool valid = false;
	};

	// Per-thread view of the HLE boundary. The active call answers "what is
	// this thread blocked in"; the last completed one answers "when did this
	// thread last cross into the emulator at all", which is what distinguishes
	// a thread waiting inside a traced call from one that has gone quiet
	// somewhere the trace cannot see.
	struct ThreadActivity {
		std::string  guest_name;
		uint64_t     entry_address = 0;
		uint64_t     host_thread_id = 0;
		bool         started       = false;
		bool         exited        = false;
		uint64_t     started_us    = 0;
		uint64_t     exited_us     = 0;
		uint64_t     calls         = 0;
		std::string  last_call;
		uint64_t     last_caller   = 0;
		uint64_t     last_exit_us  = 0;
		HleCallToken active_token  = 0;
	};

	struct LastShaderPhase {
		std::string stage;
		uint64_t hash = 0;
		std::string phase;
		uint64_t time_us = 0;
		bool valid = false;
	};

	// `key` identifies a run of equivalent events. Consecutive events sharing a
	// key are counted rather than stored, so one loop cannot evict the history
	// that led to it. An empty key never collapses.
	void PushRecentLocked(std::string key, std::string event);
	void PushLineLocked(std::string event);
	static std::string HleName(const HleCall& call);

	mutable std::mutex m_mutex;
	size_t m_recent_capacity = 0;
	HleCallToken m_next_hle_token = 1;
	std::unordered_map<HleCallToken, HleCall> m_active_hle;
	std::map<uint32_t, ThreadActivity> m_threads;
	std::deque<std::string> m_recent_events;
	std::string             m_recent_repeat_key;
	uint64_t                m_recent_repeat_count = 0;
	std::vector<std::string> m_trace;
	size_t                   m_trace_limit = 0;

	uint64_t m_gpu_dispatches = 0;
	uint64_t m_gpu_draws = 0;
	LastDispatch m_last_dispatch;
	LastDraw m_last_draw;

	uint64_t m_video_cpu_submitted = 0;
	uint64_t m_video_gpu_submitted = 0;
	uint64_t m_video_prepared = 0;
	uint64_t m_video_ready = 0;
	uint64_t m_video_presented = 0;
	int m_last_submitted_buffer = -1;
	int m_last_presented_buffer = -1;

	LastShaderPhase m_last_shader_phase;
};

[[nodiscard]] bool Enabled() noexcept;

// Renders a guest address as "<module>+0x<offset>". The diagnostics layer has
// no business knowing about the loader, so the loader installs this instead;
// with no resolver the report still carries the raw address.
using ModuleResolver = std::string (*)(uint64_t address);
void SetModuleResolver(ModuleResolver resolver) noexcept;
[[nodiscard]] uint64_t NowUs() noexcept;
State& GlobalState();
void Initialize();

class HleScope final {
public:
	HleScope(uint32_t thread_id, const char* library, const char* module, const char* function,
	         uint64_t caller_address);
	~HleScope();
	HleScope(const HleScope&) = delete;
	HleScope& operator=(const HleScope&) = delete;

private:
	HleCallToken m_token = 0;
};

} // namespace Libs::RuntimeDiagnostics
