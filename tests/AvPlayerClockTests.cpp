#include "libs/avPlayerClock.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

namespace {

using Libs::Audio::AvPlayer::AudioFrameDue;
using Libs::Audio::AvPlayer::AudioLeadMs;
using Libs::Audio::AvPlayer::MediaClock;
using Libs::Audio::AvPlayer::VideoFrameDue;
using Libs::Audio::AvPlayer::VideoToleranceMs;

void Check(bool value, const char* message) {
	if (!value) {
		std::fprintf(stderr, "AvPlayerClockTests: failed: %s\n", message);
		std::abort();
	}
}

// ---------------------------------------------------------------------------
// MediaClock
// ---------------------------------------------------------------------------

void TestClockAdvancesWithRealTime() {
	MediaClock clock;
	Check(!clock.Running() && clock.PositionMs(0) == 0, "a fresh clock reported a position");

	clock.Start(0, 1'000'000);
	Check(clock.Running(), "Start did not run the clock");
	Check(clock.PositionMs(1'000'000) == 0, "the clock did not start at its start position");
	Check(clock.PositionMs(1'500'000) == 500, "the clock did not advance with real time");

	clock.Stop();
	Check(!clock.Running() && clock.PositionMs(9'000'000) == 0, "Stop left the clock running");
}

void TestClockStartsAtSeekPosition() {
	MediaClock clock;
	clock.Start(12'000, 1'000'000);
	Check(clock.PositionMs(1'000'000) == 12'000, "the clock ignored the start position");
	Check(clock.PositionMs(3'000'000) == 14'000, "the clock lost the start position");
}

void TestPauseFreezesTheTimeline() {
	MediaClock clock;
	clock.Start(0, 0);
	Check(clock.PositionMs(2'000'000) == 2'000, "the clock did not advance before the pause");

	clock.Pause(2'000'000);
	Check(clock.Paused(), "Pause did not take effect");
	Check(clock.PositionMs(9'000'000) == 2'000, "a paused clock kept advancing");

	clock.Resume(9'000'000);
	Check(!clock.Paused(), "Resume did not take effect");
	Check(clock.PositionMs(9'000'000) == 2'000, "Resume moved the timeline");
	Check(clock.PositionMs(10'000'000) == 3'000, "the clock did not resume from where it paused");
}

void TestAnchorPullsForwardOnlyOnce() {
	MediaClock clock;
	clock.Start(0, 0);
	// A container whose first presentation timestamp is not zero: the clock has
	// to move up to it, otherwise every frame gate stalls until real time
	// catches up with the container's own offset.
	clock.Anchor(5'000, 0);
	Check(clock.Anchored(), "Anchor did not record that it ran");
	Check(clock.PositionMs(0) == 5'000, "Anchor did not pull the clock forward");
	Check(clock.PositionMs(1'000'000) == 6'000, "the anchored clock stopped advancing");

	// Anchoring is once per playback, and never backwards: moving back would
	// re-present frames the title has already been given.
	clock.Anchor(0, 1'000'000);
	Check(clock.PositionMs(1'000'000) == 6'000, "a second Anchor moved the clock");

	MediaClock behind;
	behind.Start(0, 0);
	Check(behind.PositionMs(3'000'000) == 3'000, "the clock did not advance");
	behind.Anchor(100, 3'000'000);
	Check(behind.PositionMs(3'000'000) == 3'000, "Anchor moved the clock backwards");
}

void TestDueGates() {
	Check(VideoFrameDue(0, 0), "a frame at the clock position was not due");
	Check(!VideoFrameDue(34, 33), "a future frame was due");
	Check(VideoFrameDue(34, 33, 16), "a frame inside the tolerance was not due");
	Check(!VideoFrameDue(50, 33, 16), "a frame beyond the tolerance was due");
	Check(VideoToleranceMs(30, 1) == 16, "the 30 fps tolerance was not half a frame");
	Check(VideoToleranceMs(60, 1) == 8, "the 60 fps tolerance was not half a frame");
	Check(VideoToleranceMs(30000, 1001) == 16, "the 29.97 fps tolerance was wrong");
	Check(VideoToleranceMs(0, 0) == 0, "an undeclared frame rate produced a tolerance");
	Check(AudioFrameDue(AudioLeadMs, 0), "audio inside the lead was not due");
	Check(!AudioFrameDue(AudioLeadMs + 1, 0), "audio beyond the lead was due");
}

// ---------------------------------------------------------------------------
// Refresh-rate independence
//
// The simulation below reproduces the shape of the real delivery path: decoded
// frames sit in bounded queues that the decoder refills as soon as there is
// room (decoding is far faster than real time, which is exactly why delivery
// has to be paced), and the title pulls from those queues once per host frame.
// The only thing that varies between runs is how often the title pulls.
// ---------------------------------------------------------------------------

constexpr uint64_t StreamDurationMs = 4000;
constexpr uint64_t VideoToleranceMsValue = 1000 / (2 * 30); // half a 30 fps frame
constexpr size_t   VideoQueueDepth  = 2;
constexpr size_t   AudioQueueDepth  = 8;

std::vector<uint64_t> VideoTimestamps() {
	std::vector<uint64_t> out;
	for (uint64_t i = 0; i * 1000 / 30 < StreamDurationMs; i++) {
		out.push_back(i * 1000 / 30); // 30 fps
	}
	return out;
}

std::vector<uint64_t> AudioTimestamps() {
	std::vector<uint64_t> out;
	for (uint64_t i = 0; i * 1024 * 1000 / 48000 < StreamDurationMs; i++) {
		out.push_back(i * 1024 * 1000 / 48000); // 1024-sample AAC frames at 48 kHz
	}
	return out;
}

struct Playback {
	uint64_t eos_host_ms      = 0;
	uint64_t video_delivered  = 0;
	uint64_t video_dropped    = 0;
	uint64_t audio_delivered  = 0;
};

// One producer/consumer pair: `produced` frames have left the decoder, `queue`
// holds the ones waiting for the title.
struct Track {
	const std::vector<uint64_t>* stamps = nullptr;
	size_t                       produced = 0;
	size_t                       depth    = 0;
	std::deque<uint64_t>         queue;

	void Refill() {
		while (queue.size() < depth && produced < stamps->size()) {
			queue.push_back((*stamps)[produced++]);
		}
	}
	[[nodiscard]] bool Drained() const { return produced == stamps->size() && queue.empty(); }
};

// The fixed policy: both tracks are gated by the media clock.
Playback RunPaced(uint32_t host_hz) {
	const auto video_stamps = VideoTimestamps();
	const auto audio_stamps = AudioTimestamps();

	Track video {&video_stamps, 0, VideoQueueDepth, {}};
	Track audio {&audio_stamps, 0, AudioQueueDepth, {}};
	video.Refill();
	audio.Refill();

	MediaClock clock;
	clock.Start(0, 0);
	clock.Anchor(video_stamps.front(), 0);

	Playback out;
	const uint64_t tick_us = 1'000'000 / host_hz;
	for (uint64_t tick = 0; tick < host_hz * 60; tick++) {
		const uint64_t now_us = tick * tick_us;
		if (video.Drained() && audio.Drained()) {
			out.eos_host_ms = now_us / 1000;
			return out;
		}
		const uint64_t media_ms = clock.PositionMs(now_us);

		// Video: take the newest frame that is due, dropping any it supersedes.
		bool delivered = false;
		while (!video.queue.empty() &&
		       VideoFrameDue(video.queue.front(), media_ms, VideoToleranceMsValue)) {
			video.queue.pop_front();
			video.Refill();
			if (delivered) {
				out.video_dropped++;
			}
			delivered = true;
		}
		if (delivered) {
			out.video_delivered++;
		}

		// Audio: hand over everything inside the lead.
		while (!audio.queue.empty() && AudioFrameDue(audio.queue.front(), media_ms)) {
			audio.queue.pop_front();
			audio.Refill();
			out.audio_delivered++;
		}
	}
	Check(false, "the paced playback never reached end of stream");
	return out;
}

// The policy this replaced: audio was handed over on every call, and video
// followed the timestamp of the last audio frame handed over. Both are call
// counters rather than clocks, so the host frame rate becomes the play rate.
Playback RunUnpaced(uint32_t host_hz) {
	const auto video_stamps = VideoTimestamps();
	const auto audio_stamps = AudioTimestamps();

	Track video {&video_stamps, 0, VideoQueueDepth, {}};
	Track audio {&audio_stamps, 0, AudioQueueDepth, {}};
	video.Refill();
	audio.Refill();

	uint64_t last_audio_ts = 0;
	Playback out;
	const uint64_t tick_us = 1'000'000 / host_hz;
	for (uint64_t tick = 0; tick < host_hz * 60; tick++) {
		if (video.Drained() && audio.Drained()) {
			out.eos_host_ms = tick * tick_us / 1000;
			return out;
		}
		if (!video.queue.empty() && video.queue.front() <= last_audio_ts) {
			video.queue.pop_front();
			video.Refill();
			out.video_delivered++;
		}
		if (!audio.queue.empty()) {
			last_audio_ts = audio.queue.front();
			audio.queue.pop_front();
			audio.Refill();
			out.audio_delivered++;
		}
	}
	Check(false, "the unpaced playback never reached end of stream");
	return out;
}

uint64_t AbsDiff(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }

void TestMediaTimelineIsRefreshRateIndependent() {
	const uint32_t cadences[] = {30, 60, 120};
	Playback       runs[3];
	for (size_t i = 0; i < 3; i++) {
		runs[i] = RunPaced(cadences[i]);
	}

	const uint64_t video_frames = VideoTimestamps().size();
	const uint64_t audio_frames = AudioTimestamps().size();

	for (size_t i = 0; i < 3; i++) {
		// The stream runs out at its own duration, whatever the host does.
		Check(AbsDiff(runs[i].eos_host_ms, StreamDurationMs) <= 60,
		      "end of stream did not land on the stream duration");
		// Every frame the stream contains is handed over exactly once.
		Check(runs[i].video_delivered + runs[i].video_dropped == video_frames,
		      "the paced run lost or duplicated video frames");
		Check(runs[i].audio_delivered == audio_frames,
		      "the paced run lost or duplicated audio frames");
	}
	// Dropping exists to keep the timeline real time when a title polls slower
	// than the stream. None of these cadences is slower than 30 fps, so with
	// nearest-frame selection every frame is presented exactly once: the drift
	// between the clock and the timestamp grid must not cost frames.
	for (size_t i = 0; i < 3; i++) {
		Check(runs[i].video_dropped == 0, "a 30 fps stream dropped frames");
		Check(runs[i].video_delivered == video_frames,
		      "a frame was not presented exactly once");
	}

	// A title that polls slower than the stream still has to see the stream end
	// at its own duration: that is what the drop path is for. Half the frames go
	// unseen, which is the correct outcome -- the alternative is playing the
	// movie at half speed.
	const auto slow_poll = RunPaced(15);
	Check(AbsDiff(slow_poll.eos_host_ms, StreamDurationMs) <= 80,
	      "a title polling below the stream frame rate stretched the media duration");
	Check(slow_poll.video_delivered + slow_poll.video_dropped == video_frames,
	      "the slow-poll run lost or duplicated video frames");
	Check(slow_poll.video_dropped > 0, "the slow-poll run never engaged the drop path");
	Check(slow_poll.audio_delivered == audio_frames,
	      "the slow-poll run lost or duplicated audio frames");

	const uint64_t lo = std::min({runs[0].eos_host_ms, runs[1].eos_host_ms, runs[2].eos_host_ms});
	const uint64_t hi = std::max({runs[0].eos_host_ms, runs[1].eos_host_ms, runs[2].eos_host_ms});
	Check(hi - lo <= 60, "the host refresh rate changed the media duration");
}

void TestUnpacedDeliveryTracksTheHostRate() {
	// Characterises the defect this replaced, so the regression is visible
	// rather than asserted only by its absence: with call-rate delivery the
	// stream ran out after (duration / host rate), which is why a title that
	// waits for end of stream restarted the movie at 60 Hz but not at 30 Hz.
	const auto slow = RunUnpaced(30);
	const auto fast = RunUnpaced(60);
	const auto very_fast = RunUnpaced(120);

	Check(slow.eos_host_ms > StreamDurationMs,
	      "the unpaced 30 Hz run did not play the stream slowly");
	Check(fast.eos_host_ms < StreamDurationMs,
	      "the unpaced 60 Hz run did not reach end of stream early");
	Check(very_fast.eos_host_ms < fast.eos_host_ms && fast.eos_host_ms < slow.eos_host_ms,
	      "the unpaced runs did not scale with the host rate");
}

} // namespace

int main() {
	TestClockAdvancesWithRealTime();
	TestClockStartsAtSeekPosition();
	TestPauseFreezesTheTimeline();
	TestAnchorPullsForwardOnlyOnce();
	TestDueGates();
	TestMediaTimelineIsRefreshRateIndependent();
	TestUnpacedDeliveryTracksTheHostRate();
	std::printf("AvPlayerClockTests: all tests passed\n");
	return 0;
}
