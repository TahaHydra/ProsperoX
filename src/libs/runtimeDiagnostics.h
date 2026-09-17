#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Libs::RuntimeDiagnostics {

using HleCallToken = uint64_t;

class State final {
public:
	explicit State(size_t recent_capacity = 128);

	HleCallToken EnterHle(uint32_t thread_id, const char* library, const char* module, const char* function,
	                      uint64_t now_us);
	void ExitHle(HleCallToken token, uint64_t now_us);

	void RecordGpuDispatch(uint64_t submission_id, uint64_t shader_address, uint32_t groups_x, uint32_t groups_y,
	                       uint32_t groups_z, uint64_t now_us);
	void RecordGpuDraw(uint64_t submission_id, const char* kind, uint64_t now_us);

	void RecordVideoSubmit(bool gpu_submit, int buffer_index, uint64_t now_us);
	void RecordVideoPrepared(uint64_t now_us);
	void RecordVideoReady(uint64_t now_us);
	void RecordVideoPresented(int buffer_index, uint64_t now_us);

	void RecordShaderPhase(const char* stage, uint64_t shader_hash, const char* phase, uint64_t now_us);

	[[nodiscard]] std::string BuildReport(uint64_t now_us) const;
	[[nodiscard]] size_t RecentEventCount() const;

private:
	struct HleCall {
		uint32_t thread_id = 0;
		std::string library;
		std::string module;
		std::string function;
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

	struct LastShaderPhase {
		std::string stage;
		uint64_t hash = 0;
		std::string phase;
		uint64_t time_us = 0;
		bool valid = false;
	};

	void PushRecentLocked(std::string event);
	static std::string HleName(const HleCall& call);

	mutable std::mutex m_mutex;
	size_t m_recent_capacity = 0;
	HleCallToken m_next_hle_token = 1;
	std::unordered_map<HleCallToken, HleCall> m_active_hle;
	std::deque<std::string> m_recent_events;

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

} // namespace Libs::RuntimeDiagnostics
