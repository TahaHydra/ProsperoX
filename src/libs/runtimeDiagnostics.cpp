#include "libs/runtimeDiagnostics.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace Libs::RuntimeDiagnostics {

namespace {

uint64_t AgeMs(uint64_t now_us, uint64_t then_us) {
	return now_us >= then_us ? (now_us - then_us) / 1000 : 0;
}

const char* Safe(const char* value) {
	return value != nullptr ? value : "<null>";
}

} // namespace

State::State(size_t recent_capacity): m_recent_capacity(recent_capacity) {}

std::string State::HleName(const HleCall& call) {
	return call.library + "::" + call.module + "::" + call.function;
}

void State::PushRecentLocked(std::string event) {
	if (m_recent_capacity == 0) {
		return;
	}
	while (m_recent_events.size() >= m_recent_capacity) {
		m_recent_events.pop_front();
	}
	m_recent_events.push_back(std::move(event));
}

HleCallToken State::EnterHle(uint32_t thread_id, const char* library, const char* module, const char* function,
                             uint64_t now_us) {
	std::lock_guard lock(m_mutex);

	const auto token = m_next_hle_token++;
	HleCall call {.thread_id = thread_id,
	              .library = Safe(library),
	              .module = Safe(module),
	              .function = Safe(function),
	              .entered_us = now_us};
	const auto name = HleName(call);
	m_active_hle.emplace(token, std::move(call));

	std::ostringstream event;
	event << "ENTER " << name << " tid=" << thread_id;
	PushRecentLocked(event.str());
	return token;
}

void State::ExitHle(HleCallToken token, uint64_t now_us) {
	std::lock_guard lock(m_mutex);

	const auto it = m_active_hle.find(token);
	if (it == m_active_hle.end()) {
		return;
	}

	std::ostringstream event;
	event << "EXIT " << HleName(it->second) << " tid=" << it->second.thread_id
	      << " duration_ms=" << AgeMs(now_us, it->second.entered_us);
	PushRecentLocked(event.str());
	m_active_hle.erase(it);
}

void State::RecordGpuDispatch(uint64_t submission_id, uint64_t shader_address, uint32_t groups_x, uint32_t groups_y,
                              uint32_t groups_z, uint64_t now_us) {
	std::lock_guard lock(m_mutex);
	++m_gpu_dispatches;
	m_last_dispatch = {.submission_id = submission_id,
	                   .shader_address = shader_address,
	                   .groups_x = groups_x,
	                   .groups_y = groups_y,
	                   .groups_z = groups_z,
	                   .time_us = now_us,
	                   .valid = true};
}

void State::RecordGpuDraw(uint64_t submission_id, const char* kind, uint64_t now_us) {
	std::lock_guard lock(m_mutex);
	++m_gpu_draws;
	m_last_draw = {.submission_id = submission_id, .kind = Safe(kind), .time_us = now_us, .valid = true};
}

void State::RecordVideoSubmit(bool gpu_submit, int buffer_index, uint64_t /*now_us*/) {
	std::lock_guard lock(m_mutex);
	if (gpu_submit) {
		++m_video_gpu_submitted;
	} else {
		++m_video_cpu_submitted;
	}
	m_last_submitted_buffer = buffer_index;
}

void State::RecordVideoPrepared(uint64_t /*now_us*/) {
	std::lock_guard lock(m_mutex);
	++m_video_prepared;
}

void State::RecordVideoReady(uint64_t /*now_us*/) {
	std::lock_guard lock(m_mutex);
	++m_video_ready;
}

void State::RecordVideoPresented(int buffer_index, uint64_t /*now_us*/) {
	std::lock_guard lock(m_mutex);
	++m_video_presented;
	m_last_presented_buffer = buffer_index;
}

void State::RecordShaderPhase(const char* stage, uint64_t shader_hash, const char* phase, uint64_t now_us) {
	std::lock_guard lock(m_mutex);
	m_last_shader_phase = {.stage = Safe(stage),
	                       .hash = shader_hash,
	                       .phase = Safe(phase),
	                       .time_us = now_us,
	                       .valid = true};
}

std::string State::BuildReport(uint64_t now_us) const {
	std::lock_guard lock(m_mutex);
	std::ostringstream out;

	out << "RUNTIME_DIAG\n";
	out << "active_hle=" << m_active_hle.size() << '\n';
	for (const auto& [token, call]: m_active_hle) {
		out << "  token=" << token << " tid=" << call.thread_id << " " << HleName(call)
		    << " active_ms=" << AgeMs(now_us, call.entered_us) << '\n';
	}

	out << "recent_hle=" << m_recent_events.size() << '\n';
	for (const auto& event: m_recent_events) {
		out << "  " << event << '\n';
	}

	out << "GPU dispatches=" << m_gpu_dispatches << " draws=" << m_gpu_draws << '\n';
	if (m_last_dispatch.valid) {
		out << "  last_dispatch submission=" << m_last_dispatch.submission_id << " shader=0x" << std::hex
		    << m_last_dispatch.shader_address << std::dec << " groups=" << m_last_dispatch.groups_x << 'x'
		    << m_last_dispatch.groups_y << 'x' << m_last_dispatch.groups_z
		    << " age_ms=" << AgeMs(now_us, m_last_dispatch.time_us) << '\n';
	}
	if (m_last_draw.valid) {
		out << "  last_draw submission=" << m_last_draw.submission_id << " kind=" << m_last_draw.kind
		    << " age_ms=" << AgeMs(now_us, m_last_draw.time_us) << '\n';
	}

	out << "VIDEO cpu_submitted=" << m_video_cpu_submitted << " gpu_submitted=" << m_video_gpu_submitted
	    << " prepared=" << m_video_prepared << " ready=" << m_video_ready << " presented=" << m_video_presented
	    << " last_submitted_buffer=" << m_last_submitted_buffer << " last_presented_buffer=" << m_last_presented_buffer
	    << '\n';

	if (m_last_shader_phase.valid) {
		out << "SHADER stage=" << m_last_shader_phase.stage << " hash=" << std::hex << std::setfill('0')
		    << std::setw(16) << m_last_shader_phase.hash << std::dec << " phase=" << m_last_shader_phase.phase
		    << " age_ms=" << AgeMs(now_us, m_last_shader_phase.time_us) << '\n';
	} else {
		out << "SHADER none\n";
	}

	return out.str();
}

size_t State::RecentEventCount() const {
	std::lock_guard lock(m_mutex);
	return m_recent_events.size();
}

} // namespace Libs::RuntimeDiagnostics
