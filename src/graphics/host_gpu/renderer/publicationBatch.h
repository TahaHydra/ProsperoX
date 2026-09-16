#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PUBLICATIONBATCH_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PUBLICATIONBATCH_H_

#include <cstdint>

namespace Libs::Graphics {

// Decides when an end-of-pipe completion has to reach the device.
//
// A completion becomes guest-visible only once its tick retires, so every
// completion needs its recording submitted. It does not need its *own*
// submission. The distinction that matters is whether a guest thread can be
// blocked on the completion right now:
//
//   * An interrupt or a flip wakes an event queue. A guest thread may already
//     be parked in sceKernelWaitEqueue for it, and nothing else will move the
//     recording along, so the submission must happen immediately.
//   * A plain label write is observed by polling. The poller cannot make
//     progress before the GPU reaches the packet anyway, so the submission only
//     has to happen before the command processor stops producing work.
//
// The batch therefore coalesces plain label writes up to a limit, and the
// command processor flushes unconditionally when a slice ends - which is every
// point at which it yields, completes, or suspends on a wait. That bounds the
// deferral to "work the command processor was going to record anyway", never to
// a timer and never across a guest wait.
class PublicationBatch final {
public:
	static constexpr uint32_t DefaultLimit = 8;

	constexpr PublicationBatch() noexcept = default;
	constexpr explicit PublicationBatch(uint32_t limit) noexcept: m_limit(Clamp(limit)) {}

	constexpr void SetLimit(uint32_t limit) noexcept { m_limit = Clamp(limit); }
	[[nodiscard]] constexpr uint32_t Limit() const noexcept { return m_limit; }

	// The recording carries a completion a guest thread can be blocked on.
	constexpr void RequirePrompt() noexcept { m_prompt = true; }

	// Records one end-of-pipe completion. Returns true when the recording has to
	// be submitted now.
	[[nodiscard]] constexpr bool Record() noexcept {
		m_pending++;
		return m_prompt || m_pending >= m_limit;
	}

	// A new recording started: nothing is pending against it.
	constexpr void Reset() noexcept {
		m_pending = 0;
		m_prompt  = false;
	}

	[[nodiscard]] constexpr uint32_t Pending() const noexcept { return m_pending; }
	[[nodiscard]] constexpr bool     Prompt() const noexcept { return m_prompt; }

private:
	// A limit of zero would defer forever; one restores submit-per-completion.
	static constexpr uint32_t Clamp(uint32_t limit) noexcept { return limit == 0 ? 1 : limit; }

	uint32_t m_limit   = DefaultLimit;
	uint32_t m_pending = 0;
	bool     m_prompt  = false;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PUBLICATIONBATCH_H_
