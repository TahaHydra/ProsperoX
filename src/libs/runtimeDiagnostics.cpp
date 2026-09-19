#include "libs/runtimeDiagnostics.h"

#include "graphics/gpuStats.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

namespace Libs::RuntimeDiagnostics {

namespace {

std::atomic_bool                g_enabled {false};
std::once_flag                  g_initialize_once;
std::atomic<ModuleResolver>     g_module_resolver {nullptr};

// Formats a guest return address. The resolver runs only while a report is
// being built, never on the call path.
std::string DescribeAddress(uint64_t address) {
	if (address == 0) {
		return {};
	}
	std::ostringstream out;
	out << " caller=0x" << std::hex << address << std::dec;
	if (const auto resolver = g_module_resolver.load(std::memory_order_acquire);
	    resolver != nullptr) {
		auto described = resolver(address);
		if (!described.empty()) {
			out << " (" << described << ')';
		}
	}
	return out.str();
}

uint64_t AgeMs(uint64_t now_us, uint64_t then_us) {
	return now_us >= then_us ? (now_us - then_us) / 1000 : 0;
}

const char* Safe(const char* value) {
	return value != nullptr ? value : "<null>";
}

bool EnvEnabled(const char* name) {
	const auto* value = std::getenv(name);
	return value != nullptr && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
}

void AppendSnapshot(uint64_t sequence) {
	std::ofstream output("_RuntimeDiag.txt", std::ios::out | std::ios::app);
	if (!output.is_open()) {
		return;
	}
	const auto now_us = NowUs();
	output << "========== PROSPEROX RUNTIME DIAG #" << sequence << " t_ms=" << (now_us / 1000)
	       << " ==========\n";
	output << GlobalState().BuildReport(now_us);
	const auto gpu_report = Graphics::Stats::LatestReport();
	if (!gpu_report.empty()) {
		output << "GPU_STATS_LAST\n" << gpu_report;
	} else {
		output << "GPU_STATS_LAST none\n";
	}
	output << "============================================================\n";
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
                             uint64_t caller_address, uint64_t now_us) {
	std::lock_guard lock(m_mutex);

	const auto token = m_next_hle_token++;
	HleCall call {.thread_id = thread_id,
	              .library = Safe(library),
	              .module = Safe(module),
	              .function = Safe(function),
	              .caller_address = caller_address,
	              .entered_us = now_us};
	const auto name = HleName(call);
	m_active_hle.emplace(token, std::move(call));

	auto& thread = m_threads[thread_id];
	thread.calls++;
	thread.last_call    = name;
	thread.last_caller  = caller_address;
	thread.active_token = token;

	std::ostringstream event;
	event << "ENTER " << name << " tid=" << thread_id;
	PushRecentLocked(event.str());
	return token;
}

void State::RecordGuestThreadStart(uint32_t thread_id, const char* name, uint64_t entry_address,
                                   uint64_t host_thread_id, uint64_t now_us) {
	std::lock_guard lock(m_mutex);
	auto& thread          = m_threads[thread_id];
	thread.guest_name     = Safe(name);
	thread.entry_address  = entry_address;
	thread.host_thread_id = host_thread_id;
	thread.started       = true;
	thread.exited        = false;
	thread.started_us    = now_us;

	std::ostringstream event;
	event << "THREAD_START tid=" << thread_id << " name=" << Safe(name) << " entry=0x" << std::hex
	      << entry_address << std::dec << " host_tid=" << host_thread_id;
	PushRecentLocked(event.str());
}

void State::RecordGuestThreadExit(uint32_t thread_id, uint64_t now_us) {
	std::lock_guard lock(m_mutex);
	auto& thread     = m_threads[thread_id];
	thread.exited    = true;
	thread.exited_us = now_us;

	std::ostringstream event;
	event << "THREAD_EXIT tid=" << thread_id;
	PushRecentLocked(event.str());
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

	auto& thread = m_threads[it->second.thread_id];
	thread.last_call     = HleName(it->second);
	thread.last_caller   = it->second.caller_address;
	thread.last_exit_us  = now_us;
	if (thread.active_token == token) {
		thread.active_token = 0;
	}
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
		    << " active_ms=" << AgeMs(now_us, call.entered_us)
		    << DescribeAddress(call.caller_address) << '\n';
	}

	// One line per guest thread the emulator has ever seen crossing the HLE
	// boundary. A stall shows up here directly: either the thread is inside a
	// call and `active_ms` keeps growing, or it is outside one and `silent_ms`
	// does, which says the thread is somewhere the HLE trace cannot follow.
	out << "threads=" << m_threads.size() << '\n';
	for (const auto& [tid, thread]: m_threads) {
		out << "  tid=" << tid;
		if (!thread.guest_name.empty()) {
			out << " name=" << thread.guest_name;
		}
		if (thread.entry_address != 0) {
			out << " entry=0x" << std::hex << thread.entry_address << std::dec;
		}
		// The host thread this guest thread runs on, so a thread that is silent
		// because it is executing guest code -- which no HLE trace can follow --
		// can still be found in an external profiler or debugger.
		if (thread.host_thread_id != 0) {
			out << " host_tid=" << thread.host_thread_id;
		}
		out << " calls=" << thread.calls;
		if (thread.exited) {
			out << " state=exited exited_ms_ago=" << AgeMs(now_us, thread.exited_us);
		} else if (thread.active_token != 0) {
			const auto active = m_active_hle.find(thread.active_token);
			if (active != m_active_hle.end()) {
				out << " state=in_hle call=" << HleName(active->second)
				    << " active_ms=" << AgeMs(now_us, active->second.entered_us)
				    << DescribeAddress(active->second.caller_address);
			} else {
				out << " state=in_hle call=" << thread.last_call;
			}
		} else if (thread.last_exit_us != 0) {
			out << " state=guest last_call=" << thread.last_call
			    << " silent_ms=" << AgeMs(now_us, thread.last_exit_us)
			    << DescribeAddress(thread.last_caller);
		} else if (thread.started) {
			out << " state=running started_ms_ago=" << AgeMs(now_us, thread.started_us);
		} else {
			out << " state=unknown";
		}
		out << '\n';
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

bool Enabled() noexcept {
	return g_enabled.load(std::memory_order_acquire);
}

void SetModuleResolver(ModuleResolver resolver) noexcept {
	g_module_resolver.store(resolver, std::memory_order_release);
}

uint64_t NowUs() noexcept {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
	                                 std::chrono::steady_clock::now().time_since_epoch())
	                                 .count());
}

State& GlobalState() {
	static State state(128);
	return state;
}

void Initialize() {
	std::call_once(g_initialize_once, [] {
		if (!EnvEnabled("PROSPEROX_RUNTIME_DIAG")) {
			return;
		}

		(void)GlobalState();
		{
			std::ofstream output("_RuntimeDiag.txt", std::ios::out | std::ios::trunc);
			if (output.is_open()) {
				output << "ProsperoX passive runtime diagnostics enabled\n";
			}
		}
		g_enabled.store(true, std::memory_order_release);

		std::thread([] {
			uint64_t sequence = 0;
			while (Enabled()) {
				std::this_thread::sleep_for(std::chrono::seconds(2));
				if (Enabled()) {
					AppendSnapshot(++sequence);
				}
			}
		}).detach();
	});
}

HleScope::HleScope(uint32_t thread_id, const char* library, const char* module, const char* function,
                   uint64_t caller_address) {
	if (Enabled()) {
		m_token = GlobalState().EnterHle(thread_id, library, module, function, caller_address, NowUs());
	}
}

HleScope::~HleScope() {
	if (m_token != 0) {
		GlobalState().ExitHle(m_token, NowUs());
	}
}

} // namespace Libs::RuntimeDiagnostics
