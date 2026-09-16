#ifndef GRAPHICS_GUEST_GPU_COMMAND_PROCESSOR_PENDING_COMPLETIONS_H
#define GRAPHICS_GUEST_GPU_COMMAND_PROCESSOR_PENDING_COMPLETIONS_H

#include <cstdint>
#include <iterator>
#include <unordered_map>

namespace Libs::Graphics {

// End-of-pipe completions one command processor has recorded into its own
// stream and that have not retired yet.
//
// While an entry is present, three things hold: the value the device will write
// to that address is known, the write is still ahead of the current recording
// position, and everything recorded from here executes after it on the same
// host queue. That is what lets a WAIT_REG_MEM for the same value be answered
// from submission order instead of from a round trip to the CPU and back.
//
// An entry stops being usable the moment either of those stops holding, so the
// container is deliberately strict:
//   * a retired tick is pruned, because after it the value is on its way into
//     guest memory and a later wait must match a new completion rather than a
//     past one;
//   * a direct command-stream write to the address drops it, because the
//     stream is now managing those bytes itself.
class PendingCompletions final {
public:
	void Record(uint64_t address, uint32_t width, uint64_t value, uint64_t tick) {
		if (address == 0 || width == 0) {
			return;
		}
		m_entries[address] = Entry {value, tick, width};
	}

	// Forgets every completion overlapping the range.
	void Drop(uint64_t address, uint64_t size) {
		if (m_entries.empty() || size == 0) {
			return;
		}
		for (auto it = m_entries.begin(); it != m_entries.end();) {
			const bool overlaps =
			    it->first < address + size && address < it->first + it->second.width;
			it = overlaps ? m_entries.erase(it) : std::next(it);
		}
	}

	// Forgets every completion the device has already reached.
	void Prune(uint64_t retired_tick) {
		for (auto it = m_entries.begin(); it != m_entries.end();) {
			it = retired_tick >= it->second.tick ? m_entries.erase(it) : std::next(it);
		}
	}

	// The value a pending completion of exactly this width will write, if any.
	// A different width is not a match: the guest is describing other bytes.
	[[nodiscard]] bool Find(uint64_t address, uint32_t width, uint64_t& value) const {
		const auto entry = m_entries.find(address);
		if (entry == m_entries.end() || entry->second.width != width) {
			return false;
		}
		value = entry->second.value;
		return true;
	}

	void                        Clear() noexcept { m_entries.clear(); }
	[[nodiscard]] bool          Empty() const noexcept { return m_entries.empty(); }
	[[nodiscard]] std::size_t   Size() const noexcept { return m_entries.size(); }

private:
	struct Entry {
		uint64_t value = 0;
		uint64_t tick  = 0;
		uint32_t width = 0;
	};

	std::unordered_map<uint64_t, Entry> m_entries;
};

} // namespace Libs::Graphics

#endif // GRAPHICS_GUEST_GPU_COMMAND_PROCESSOR_PENDING_COMPLETIONS_H
