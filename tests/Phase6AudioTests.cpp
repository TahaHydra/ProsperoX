#define SDL_MAIN_HANDLED
#include "SDL.h"
#include <map>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>

// Test-local headless sink at the SDL transport boundary. Production PCM
// preparation, gain, channel routing, conversion and error handling run intact.
namespace Sink {
struct Device { SDL_AudioSpec spec{}; std::vector<unsigned char> bytes; };
static std::map<SDL_AudioDeviceID, Device> devices;
static SDL_AudioDeviceID next = 1;
static bool fail_open = false, fail_queue = false;
static int output_rate = 0;
static SDL_AudioFormat output_format = 0;
static Uint8 output_channels = 0;
static Uint32 forced_queued = 0;
static SDL_AudioDeviceID Open(const char*, int, const SDL_AudioSpec* desired,
                             SDL_AudioSpec* obtained, int) {
    if (fail_open) return 0;
    *obtained = *desired;
    if (output_rate) obtained->freq = output_rate;
    if (output_format) obtained->format = output_format;
    if (output_channels) obtained->channels = output_channels;
    devices[next].spec = *obtained;
    return next++;
}
static void Close(SDL_AudioDeviceID id) { devices.erase(id); }
static void Pause(SDL_AudioDeviceID, int) {}
static void Clear(SDL_AudioDeviceID id) { devices.at(id).bytes.clear(); }
static Uint32 Queued(SDL_AudioDeviceID) { return forced_queued; }
static int Queue(SDL_AudioDeviceID id, const void* data, Uint32 size) {
    if (fail_queue || !devices.contains(id)) return -1;
    auto& out = devices.at(id).bytes;
    const auto* begin = static_cast<const unsigned char*>(data);
    out.insert(out.end(), begin, begin + size);
    return 0;
}
}
#define SDL_OpenAudioDevice Sink::Open
#define SDL_CloseAudioDevice Sink::Close
#define SDL_PauseAudioDevice Sink::Pause
#define SDL_ClearQueuedAudio Sink::Clear
#define SDL_GetQueuedAudioSize Sink::Queued
#define SDL_QueueAudio Sink::Queue
#include "../src/libs/audio.cpp"
#undef SDL_OpenAudioDevice
#undef SDL_CloseAudioDevice
#undef SDL_PauseAudioDevice
#undef SDL_ClearQueuedAudio
#undef SDL_GetQueuedAudioSize
#undef SDL_QueueAudio

#include "common/emulatorConfig.h"
#include "common/subsystems.h"

static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "PHASE6_AUDIO_FAIL %s\n", message); std::exit(1); }
}

#include "Phase6SaveTests.inc"

static void StreamingAudioTest() {
    namespace AI = Libs::Audio::AudioInternal;
    // Independent analytic oracle, not another call to the converter under test.
    // Different grains expose resets at each block and accumulating rate drift.
    for (const int rate : {24000, 44100, 96000}) {
        Sink::output_rate = rate;
        std::vector<unsigned char> reference;
        for (const unsigned grain : {64u, 256u, 1024u}) {
            const int handle = AI::AudioOutOpen(0, grain, 48000, AI::Format::FloatStereo);
            Check(handle > 0, "open mismatched-rate output");
            constexpr unsigned total = 196608;
            std::vector<float> input(grain * 2);
            AI::OutputParam output{handle, input.data()};
            for (unsigned begin = 0; begin < total; begin += grain) {
                for (unsigned i = 0; i < grain; ++i) {
                    input[2*i] = static_cast<float>(0.25 * std::sin((begin+i) * 0.007));
                    input[2*i+1] = -input[2*i];
                }
                Check(AI::AudioOutOutputs(&output, 1, false) == grain, "stream accepts complete guest grain");
            }
            const auto& bytes = Sink::devices.rbegin()->second.bytes;
            Check(bytes.size() % (2 * sizeof(float)) == 0, "whole stereo output frames");
            const size_t frames = bytes.size() / (2 * sizeof(float));
            const size_t expected = uint64_t(total) * rate / 48000;
            // No end-of-stream flush is requested; bound converter lookahead
            // by 64 input frames, independent of duration or grain.
            Check(frames <= expected && expected - frames <= uint64_t(64) * rate / 48000,
                  "rate duration differs only by bounded converter lookahead");
            float maximum_error = 0;
            for (size_t i = 16; i < frames; ++i) {
                float left, right;
                std::memcpy(&left, bytes.data() + i * 8, 4);
                std::memcpy(&right, bytes.data() + i * 8 + 4, 4);
                const float expected_sample = static_cast<float>(0.25 * std::sin(i * 0.007 * 48000 / rate));
                maximum_error = std::max(maximum_error, std::abs(left - expected_sample));
                Check(std::isfinite(left) && std::abs(left + right) < 0.00001f,
                      "stream maintains independent stereo channels");
            }
            std::printf("PHASE6_STREAM rate=%d grain=%u frames=%zu expected=%zu max_error=%.6f\n",
                        rate, grain, frames, expected, maximum_error);
            Check(maximum_error < 0.003f, "continuous waveform without block-edge discontinuities or phase drift");
            if (reference.empty()) reference = bytes;
            else Check(bytes == reference, "grain partition does not change output samples or count");
            AI::AudioOutClose(handle);
        }
    }
    Sink::output_rate = 44100;
    const int handle = AI::AudioOutOpen(0, 1024, 48000, AI::Format::FloatStereo);
    Check(handle > 0, "open streaming failure fixture");
    std::vector<float> input(2048, 0.25f);
    AI::OutputParam output{handle, input.data()};
    Sink::forced_queued = 1000000;
    Check(static_cast<int32_t>(AI::AudioOutOutputs(&output, 1, true)) < 0,
          "stalled device rejects input before converter consumes it");
    Check(Sink::devices.rbegin()->second.bytes.empty(), "timeout publishes no input");
    Sink::forced_queued = 0;
    Check(AI::AudioOutOutputs(&output, 1, false) == 1024, "retry after pre-conversion timeout");
    Sink::fail_queue = true;
    Check(static_cast<int32_t>(AI::AudioOutOutputs(&output, 1, false)) < 0, "converted queue failure propagates");
    const auto retained = Sink::devices.rbegin()->second.bytes;
    Sink::fail_queue = false;
    Check(static_cast<int32_t>(AI::AudioOutOutputs(&output, 1, false)) < 0 &&
          Sink::devices.rbegin()->second.bytes == retained,
          "failed converter requires reopen rather than accepting ambiguous retry");
    AI::AudioOutClose(handle);
    const int reopened = AI::AudioOutOpen(0, 1024, 48000, AI::Format::FloatStereo);
    output.handle = reopened;
    Check(reopened > 0 && AI::AudioOutOutputs(&output, 1, false) == 1024,
          "close/reopen restores a clean converter");
    AI::AudioOutClose(reopened);
    // Exercise both conversion stages together with a negotiated mono S16 sink.
    Sink::output_format = AUDIO_S16SYS;
    Sink::output_channels = 1;
    const int converted = AI::AudioOutOpen(0, 1024, 48000, AI::Format::FloatStereo);
    output.handle = converted;
    Check(converted > 0, "open rate/format/channel conversion");
    for (unsigned i = 0; i < 8; ++i)
        Check(AI::AudioOutOutputs(&output, 1, false) == 1024, "mixed converter accepts grain");
    const auto& mono = Sink::devices.rbegin()->second.bytes;
    Check(mono.size() > 7000 * sizeof(int16_t), "mono converted duration");
    for (size_t i = 32; i < mono.size() / 2; ++i) {
        int16_t sample;
        std::memcpy(&sample, mono.data() + 2*i, 2);
        Check(std::abs(int(sample) - 8192) <= 3, "mono S16 conversion keeps signal amplitude");
    }
    AI::AudioOutClose(converted);
    Sink::output_format = 0;
    Sink::output_channels = 0;
    Sink::output_rate = 0;
    Check(Sink::devices.empty(), "stream tests release devices");
    std::puts("PHASE6_STREAM_PASS waveform=1 duration=1 timeout_retry=1 queue_failure=1 reopen=1");
}

int main(int argc, char** argv) {
    Common::InitializeThreads();
    Common::Subsystems subsystems;
    subsystems.Initialize<Config::Lifecycle>();
    Config::ConfigOptions options;
    options.printf_direction = Config::OutputDirection::Silent;
    Config::Load(options);
    subsystems.Initialize<Log::Lifecycle>();
    if (argc == 2 && std::strcmp(argv[1], "--save-store") == 0) { SaveStoreTest(); return 0; }
    if (argc == 3 && (std::strcmp(argv[1], "--save-write") == 0 || std::strcmp(argv[1], "--save-read") == 0)) {
        SaveExportsTest(argv[1], argv[2]); return 0;
    }
    subsystems.Initialize<Libs::Audio::Lifecycle>();
    if (argc == 2 && std::strcmp(argv[1], "--streaming") == 0) { StreamingAudioTest(); return 0; }
    namespace AI = Libs::Audio::AudioInternal;
    using Libs::Audio::Audio;
    const bool errors = argc == 2 && std::strcmp(argv[1], "--errors") == 0;
    if (errors) {
        Sink::fail_open = true;
        Check(AI::AudioOutOpen(0, 256, 48000, AI::Format::FloatStereo) == 0,
              "failed device open must not create a silent successful port");
        Sink::fail_open = false;
        const int handle = AI::AudioOutOpen(0, 256, 48000, AI::Format::FloatStereo);
        Check(handle > 0, "recover after failed open");
        float pcm[512]{};
        AI::OutputParam output{handle, pcm};
        Sink::fail_queue = true;
        Check(static_cast<int32_t>(AI::AudioOutOutputs(&output, 1, false)) < 0,
              "queue failure must reach caller");
        Sink::fail_queue = false;
        Check(AI::AudioOutOutputs(&output, 1, false) == 256, "retry reports accepted frames");
        AI::AudioOutClose(handle);
    } else {
        constexpr AI::Format formats[] = {AI::Format::FloatMono, AI::Format::FloatStereo,
            AI::Format::Float8Ch, AI::Format::Float8ChStd, AI::Format::Float12Ch};
        constexpr unsigned channels[] = {1, 2, 8, 8, 12};
        constexpr unsigned route[] = {0, 1, 2, 3, 6, 7, 4, 5};
        for (unsigned f = 0; f < 5; ++f) {
            const unsigned n = channels[f], out_n = std::min(n, 8u), frames = 256;
            const int handle = AI::AudioOutOpen(0, frames, 48000, formats[f]);
            Check(handle > 0, "open PCM fixture");
            // Each input channel has its own impulse at a different frame.
            std::vector<float> pcm(frames * n, 0.0f), expected(frames * out_n, 0.0f);
            for (unsigned ch = 0; ch < n; ++ch) pcm[(ch + 1) * n + ch] = 0.25f;
            for (unsigned ch = 0; ch < out_n; ++ch) {
                const unsigned src = n >= 8 && f != 3 ? route[ch] : ch;
                expected[(src + 1) * out_n + ch] = 0.25f;
            }
            if (n == 12) {
                constexpr unsigned height[] = {0, 1, 4, 5};
                for (unsigned ch = 0; ch < 4; ++ch) expected[(9 + ch) * out_n + height[ch]] = 0.25f;
            }
            AI::OutputParam output{handle, pcm.data()};
            Check(AI::AudioOutOutputs(&output, 1, false) == frames, "accepted frame count");
            const auto& bytes = Sink::devices.rbegin()->second.bytes;
            Check(bytes.size() == expected.size() * sizeof(float), "sample count and channel stride");
            Check(std::memcmp(bytes.data(), expected.data(), bytes.size()) == 0, "channel impulse routing");
            AI::AudioOutClose(handle);
            Check(Sink::devices.empty(), "close releases sink");
        }
        const int handle = AI::AudioOutOpen(0, 256, 48000, AI::Format::Signed16bitStereo);
        Check(handle > 0, "open integer fixture");
        int gains[2] = {65536, 16384};
        Check(Libs::Audio::g_audio->AudioOutSetVolume(Audio::Id(handle), 3, gains), "gain configuration");
        std::vector<int16_t> pcm(512), expected(512);
        for (unsigned i = 0; i < 256; ++i) {
            pcm[2*i] = i % 2 ? -20000 : 20000; pcm[2*i+1] = 12000;
            expected[2*i] = i % 2 ? -32768 : 32767; expected[2*i+1] = 6000;
        }
        AI::OutputParam output{handle, pcm.data()};
        Check(AI::AudioOutOutputs(&output, 1, false) == 256, "integer frame count");
        const auto& bytes = Sink::devices.rbegin()->second.bytes;
        Check(bytes.size() == expected.size()*2 && std::memcmp(bytes.data(), expected.data(), bytes.size()) == 0,
              "integer gain clipping and channel independence");
        AI::AudioOutClose(handle);
    }
    Check(Sink::devices.empty(), "no sink leak");
    std::printf("PHASE6_AUDIO_PASS mode=%s\n", errors ? "errors" : "pcm");
}
