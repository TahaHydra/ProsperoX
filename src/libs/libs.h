#ifndef EMULATOR_INCLUDE_EMULATOR_LIBS_LIBS_H_
#define EMULATOR_INCLUDE_EMULATOR_LIBS_LIBS_H_

#include "common/abi.h"
#include "common/logging/log.h"
#include "common/stringUtils.h"
#include "common/threads.h"
#include "libs/runtimeDiagnostics.h"
#include "loader/timer.h" // IWYU pragma: keep

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PRINT_NAME_ENABLED g_print_name

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PRINT_NAME_ENABLE(flag) PRINT_NAME_ENABLED = flag;

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_DEFINE(name) void name(Loader::SymbolDatabase* s)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_NAME(l, m)                                                                             \
	[[maybe_unused]] static thread_local bool PRINT_NAME_ENABLED = false;                          \
	static constexpr char                     g_library[]        = l;                              \
	static constexpr char                     g_module[]         = m;
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_VERSION(l, lv, m, mv1, mv2)                                                            \
	LIB_NAME(l, m);                                                                                \
	static constexpr int g_library_version      = lv;                                              \
	static constexpr int g_module_version_major = mv1;                                             \
	static constexpr int g_module_version_minor = mv2;
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_ADD(n, f, t)                                                                           \
	{                                                                                              \
		Loader::SymbolResolve sr {};                                                               \
		sr.name                 = n;                                                               \
		sr.library              = g_library;                                                       \
		sr.library_version      = g_library_version;                                               \
		sr.module               = g_module;                                                        \
		sr.module_version_major = g_module_version_major;                                          \
		sr.module_version_minor = g_module_version_minor;                                          \
		sr.type                 = t;                                                               \
		auto        func        = reinterpret_cast<uint64_t>(f);                                   \
		const char* dbg_name    = "" #f;                                                           \
		s->Add(sr, func, dbg_name);                                                                \
	}
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_OBJECT(n, f) LIB_ADD(n, f, Loader::SymbolType::Object)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_FUNC(n, f) LIB_ADD(n, f, Loader::SymbolType::Func)

// PRINT_NAME is used by a very large number of HLE entry points. Some functions
// expand it more than once in the same C++ scope, so the diagnostic RAII object
// must have a unique identifier for every expansion.
#define KYTY_RUNTIME_DIAG_CONCAT_INNER(a, b) a##b
#define KYTY_RUNTIME_DIAG_CONCAT(a, b) KYTY_RUNTIME_DIAG_CONCAT_INNER(a, b)

// The address PRINT_NAME() expands at belongs to an exported HLE entry point,
// so its return address is the guest call site. Recording it is what lets a
// stall report say where in the title's own code a thread stopped, rather than
// only which emulator function it stopped in.
#if defined(__has_builtin)
#if __has_builtin(__builtin_return_address)
#define KYTY_GUEST_CALLER() reinterpret_cast<uint64_t>(__builtin_return_address(0))
#endif
#endif
#ifndef KYTY_GUEST_CALLER
#define KYTY_GUEST_CALLER() uint64_t {0}
#endif

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PRINT_NAME()                                                                                   \
	[[maybe_unused]] ::Libs::RuntimeDiagnostics::HleScope                                             \
	    KYTY_RUNTIME_DIAG_CONCAT(runtime_diag_scope_, __COUNTER__)(                                   \
	        static_cast<uint32_t>(Common::Thread::GetThreadIdUnique()), g_library, g_module, __func__, \
	        KYTY_GUEST_CALLER());                                                                      \
	if (PRINT_NAME_ENABLED) {                                                                          \
		if (Log::GetDirection() != Log::Direction::Silent) {                                           \
			const auto print_name_time = Loader::Timer::GetTime().ToString("HH24:MI:SS.FFF");          \
			LOGF_COLOR(Log::Color::Cyan, "[%d][%s] %s::%s::%s()\n",                                    \
			           Common::Thread::GetThreadIdUnique(), print_name_time.c_str(), g_library,        \
			           g_module, __func__);                                                            \
		}                                                                                              \
	}

namespace Loader {
class SymbolDatabase;
} // namespace Loader

namespace Libs {

void InitAll(Loader::SymbolDatabase* s);

} // namespace Libs
#endif /* EMULATOR_INCLUDE_EMULATOR_LIBS_LIBS_H_ */
