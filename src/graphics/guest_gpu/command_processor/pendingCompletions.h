#ifndef GRAPHICS_GUEST_GPU_COMMAND_PROCESSOR_PENDING_COMPLETIONS_H
#define GRAPHICS_GUEST_GPU_COMMAND_PROCESSOR_PENDING_COMPLETIONS_H

#include <cstdint>
#include <map>

namespace Libs::Graphics {

// End-of-pipe completions one command processor has recorded into its own
// stream and that have not retired yet.
//
// While an entry is live, three things hold: the value the device will write to
// that address is known, the write is still ahead of the current recording
// position, and everything recorded from here executes after it on the same
// host queue. That is what lets a WAIT_REG_MEM for the same value be answered
// from submission order instead of from a round trip to the CPU and back.
//
// An entry stops being usable the moment either of those stops holding, so the
// container is deliberately strict:
//   * a completion whose tick has retired can no longer answer anything. The
//     device has already written the value, so a wait that still fails is
//     waiting on a newer fence, and answering it from the old one would let the
//     command processor past a reset-and-wait-again the guest performed.
//   * a direct command-stream write to the address drops it, because the stream
//     is now managing those bytes itself.
//
// Ordered by address: a command stream writing four or eight bytes must not
// walk a whole frame's completions to find out whether it touched one, and
// with waits no longer ending slices the live set spans a whole frame.
class PendingCompletions final {
public:
	// Every completion is a 32- or 64-bit label write.
	static constexpr uint64_t MaxWidth = 8;

	void Record(uint64_t address, uint32_t width, uint64_t value, uint64_t tick) {
		if (address == 0 || width == 0 || width > MaxWidth) {
			return;
		}
		m_entries[address] = Entry {value, tick, width};
	}

	// Forgets every completion overlapping the range.
	void Drop(uint64_t address, uint64_t size) {
		if (m_entries.empty() || size == 0) {
			return;
		}
		// An entry overlaps only if it starts within MaxWidth before the range.
		const auto first = address > MaxWidth ? address - MaxWidth + 1 : 0;
		const auto last  = size > UINT64_MAX - address ? UINT64_MAX : address + size;
		for (auto it = m_entries.lower_bound(first); it != m_entries.end() && it->first < last;) {
			it = address < it->first + it->second.width ? m_entries.erase(it) : std::next(it);
		}
	}

	// Forgets every completion the device has already reached.
	void Prune(uint64_t retired_tick) {
		for (auto it = m_entries.begin(); it != m_entries.end();) {
			it = retired_tick >= it->second.tick ? m_entries.erase(it) : std::next(it);
		}
	}

	// The value a live completion of exactly this width will write, if any. A
	// different width is not a match: the guest is describing other bytes. A
	// completion the device has reached is not a match either, whether or not
	// pruning has caught up with it yet.
	[[nodiscard]] bool Find(uint64_t address, uint32_t width, uint64_t retired_tick,
	                        uint64_t& value) const {
		const auto entry = m_entries.find(address);
		if (entry == m_entries.end() || entry->second.width != width ||
		    retired_tick >= entry->second.tick) {
			return false;
		}
		value = entry->second.value;
		return true;
	}

	void                      Clear() noexcept { m_entries.clear(); }
	[[nodiscard]] bool        Empty() const noexcept { return m_entries.empty(); }
	[[nodiscard]] std::size_t Size() const noexcept { return m_entries.size(); }

private:
	struct Entry {
		uint64_t value = 0;
		uint64_t tick  = 0;
		uint32_t width = 0;
	};

	std::map<uint64_t, Entry> m_entries;
};

} // namespace Libs::Graphics

#endif // GRAPHICS_GUEST_GPU_COMMAND_PROCESSOR_PENDING_COMPLETIONS_H
