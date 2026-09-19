#ifndef EMULATOR_SRC_LIBS_AVPLAYERCLOCK_H_
#define EMULATOR_SRC_LIBS_AVPLAYERCLOCK_H_

#include <cstdint>

namespace Libs::Audio::AvPlayer {

// Media timing policy for sceAvPlayer.
//
// Everything a title can observe about playback progress -- which video frame
// is due, whether an audio frame may be handed over, and when the source
// reaches end of stream -- has to be derived from elapsed real time. If it is
// derived from how often the title polls instead, then the host frame rate
// silently becomes a playback rate: a title rendering at 60 Hz drains the
// decoder twice as fast as one rendering at 30 Hz, reaches end of stream early,
// and restarts the movie. The clock below is the single source of truth that
// makes delivery independent of the poll rate.
//
// Times are milliseconds on the media timeline (the same units the guest sees
// in AvPlayerFrameInfo::time_stamp and from sceAvPlayerCurrentTime). The host
// timestamp is passed in as microseconds so tests can drive the clock directly
// rather than sleeping.
class MediaClock final {
public:
	void Start(uint64_t position_ms, uint64_t now_us) noexcept {
		m_running     = true;
		m_paused      = false;
		m_anchored    = false;
		m_position_ms = position_ms;
		m_origin_us   = now_us;
	}

	void Stop() noexcept {
		m_running  = false;
		m_paused   = false;
		m_anchored = false;
	}

	void Pause(uint64_t now_us) noexcept {
		if (!m_running || m_paused) {
			return;
		}
		// Freeze the timeline by folding elapsed time into the base position.
		m_position_ms = PositionMs(now_us);
		m_paused      = true;
	}

	void Resume(uint64_t now_us) noexcept {
		if (!m_running || !m_paused) {
			return;
		}
		m_paused    = false;
		m_origin_us = now_us;
	}

	[[nodiscard]] bool Running() const noexcept { return m_running; }
	[[nodiscard]] bool Paused() const noexcept { return m_paused; }
	[[nodiscard]] bool Anchored() const noexcept { return m_anchored; }

	// Seeking lands on the keyframe at or before the requested position, and a
	// container's first presentation timestamp need not be zero, so the first
	// decoded timestamp is what actually defines where the timeline starts.
	// Pull the clock forward to it once per playback, never backwards: moving
	// backwards would re-present frames the title has already been given.
	void Anchor(uint64_t first_frame_ms, uint64_t now_us) noexcept {
		if (!m_running || m_anchored) {
			return;
		}
		m_anchored = true;
		if (first_frame_ms > PositionMs(now_us)) {
			m_position_ms = first_frame_ms;
			m_origin_us   = now_us;
		}
	}

	[[nodiscard]] uint64_t PositionMs(uint64_t now_us) const noexcept {
		if (!m_running) {
			return 0;
		}
		if (m_paused || now_us <= m_origin_us) {
			return m_position_ms;
		}
		return m_position_ms + (now_us - m_origin_us) / 1000;
	}

private:
	bool     m_running     = false;
	bool     m_paused      = false;
	bool     m_anchored    = false;
	uint64_t m_position_ms = 0;
	uint64_t m_origin_us   = 0;
};

// How far ahead of the media clock audio may be handed over. A title keeps its
// own output ring primed, so it has to be allowed to pull ahead; but the lead
// must be bounded, because an unbounded one puts the poll rate back in charge
// of when the stream runs out. Half a second is far more than any title needs
// to prime an audio port and still bounds end of stream to the real duration.
inline constexpr uint64_t AudioLeadMs = 500;

// A video frame is due once the media clock reaches its timestamp, give or
// take a tolerance.
//
// The tolerance matters: the clock and the stream's timestamp grid are
// independent, so at matched rates (a 30 fps stream polled at 30 Hz) they drift
// against each other by a fraction of a frame. With an exact `<=` gate that
// drift alternates ticks that deliver nothing with ticks that find two frames
// due and have to drop one, which is a third of the stream discarded for no
// reason. Allowing a frame up to half a frame interval early instead selects
// the frame nearest the clock, which is the presentation the title would have
// got on hardware. It cannot make playback run fast: the clock still decides
// when the next frame becomes eligible.
[[nodiscard]] constexpr bool VideoFrameDue(uint64_t frame_ms, uint64_t media_ms,
                                           uint64_t tolerance_ms = 0) noexcept {
	return frame_ms <= media_ms + tolerance_ms;
}

// Half a frame interval, derived from the stream's own frame rate. Falls back
// to zero for a stream that does not declare one, which restores the exact
// gate rather than guessing an interval.
[[nodiscard]] constexpr uint64_t VideoToleranceMs(uint64_t frame_rate_num,
                                                  uint64_t frame_rate_den) noexcept {
	if (frame_rate_num == 0 || frame_rate_den == 0) {
		return 0;
	}
	return (1000 * frame_rate_den) / (2 * frame_rate_num);
}

// An audio frame may be handed over once the media clock is within the lead.
[[nodiscard]] constexpr bool AudioFrameDue(uint64_t frame_ms, uint64_t media_ms,
                                           uint64_t lead_ms = AudioLeadMs) noexcept {
	return frame_ms <= media_ms + lead_ms;
}

} // namespace Libs::Audio::AvPlayer

#endif // EMULATOR_SRC_LIBS_AVPLAYERCLOCK_H_
