#include "SDL.h"
#include <array>
#include <chrono>
#include <thread>

static int ControllerHardware() {
    using namespace Libs::Controller;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    Check(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0, "SDL controller initialization");
    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        auto* candidate = SDL_GameControllerOpen(i);
        if (candidate && SDL_GameControllerGetType(candidate) == SDL_CONTROLLER_TYPE_PS5) {
            pad = candidate; break;
        }
        if (candidate) SDL_GameControllerClose(candidate);
    }
    Check(pad != nullptr, "no SDL DualSense detected");
    const int id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
    Connect(id);
    std::printf("PHASE6_CONTROLLER_DEVICE name=%s vendor=%04x product=%04x instance=%d rumble=%d\n",
        SDL_GameControllerName(pad), SDL_GameControllerGetVendor(pad), SDL_GameControllerGetProduct(pad),
        id, SDL_GameControllerHasRumble(pad));
    // Bound the diagnostic pulse even if this process is interrupted.
    const int rumble_result = SDL_GameControllerRumble(pad, 0x5000, 0x5000, 300);
    std::printf("PHASE6_CONTROLLER_RUMBLE result=%d duration_ms=300\n", rumble_result);
    std::printf("PHASE6_CONTROLLER_ARMED press_Cross_to_start capture_seconds=30 arm_timeout_seconds=60\n");
    using Clock = std::chrono::steady_clock;
    const auto armed = Clock::now();
    bool started = false, previous_cross = false;
    Clock::time_point start{};
    std::array<int, 6> minimum, maximum, previous;
    minimum.fill(32767); maximum.fill(-32768); previous.fill(99999);
    uint64_t events = 0, last_timestamp = 0;
    unsigned presses = 0, releases = 0;
    while (started ? Clock::now() - start < std::chrono::seconds(30) :
                     Clock::now() - armed < std::chrono::seconds(60)) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {}
        Check(SDL_GameControllerGetAttached(pad), "controller disconnected during capture");
        const bool cross = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) != 0;
        if (!started && cross) {
            started = true; start = Clock::now();
            std::printf("PHASE6_CONTROLLER_CAPTURE_START\n");
        }
        if (started) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            if (cross != previous_cross) {
                SetButton(id, PAD_BUTTON_CROSS, cross);
                cross ? ++presses : ++releases;
                std::printf("INPUT ms=%lld button=Cross value=%d\n", static_cast<long long>(elapsed), cross);
                previous_cross = cross; ++events;
            }
            for (unsigned a = 0; a < 6; ++a) {
                const int raw = SDL_GameControllerGetAxis(pad, static_cast<SDL_GameControllerAxis>(a));
                minimum[a] = std::min(minimum[a], raw); maximum[a] = std::max(maximum[a], raw);
                const int value = controller_get_axis(a < 4 ? -32768 : 0, 32767, raw);
                SetAxis(id, static_cast<Axis>(a), value);
                PadData guest{};
                Check(PadReadState(1, &guest) == 0 && guest.connected && guest.timestamp >= last_timestamp,
                      "real input guest state/timestamp");
                last_timestamp = guest.timestamp;
                const std::array<int, 6> guest_axes{guest.left_stick_x, guest.left_stick_y,
                    guest.right_stick_x, guest.right_stick_y, guest.analog_buttons_l2, guest.analog_buttons_r2};
                Check(guest_axes[a] == value && bool(guest.buttons & PAD_BUTTON_CROSS) == cross,
                      "hardware input reaches guest state");
                if (raw != previous[a]) {
                    std::printf("INPUT ms=%lld axis=%u raw=%d guest=%d\n", static_cast<long long>(elapsed), a, raw, value);
                    previous[a] = raw; ++events;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (unsigned a = 0; a < 6; ++a)
        std::printf("PHASE6_CONTROLLER_AXIS axis=%u min=%d max=%d\n", a, minimum[a], maximum[a]);
    const bool exercised = started && presses > 0 && releases > 0 &&
        (maximum[0]-minimum[0] > 16000 || maximum[1]-minimum[1] > 16000) &&
        (maximum[2]-minimum[2] > 16000 || maximum[3]-minimum[3] > 16000) &&
        maximum[4]-minimum[4] > 16000 && maximum[5]-minimum[5] > 16000;
    std::printf("PHASE6_CONTROLLER_%s events=%llu cross_presses=%u cross_releases=%u rumble_result=%d\n",
        exercised ? "PASS" : "INCOMPLETE", static_cast<unsigned long long>(events), presses, releases, rumble_result);
    (void)SDL_GameControllerRumble(pad, 0, 0, 0);
    Disconnect(id);
    SDL_GameControllerClose(pad);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    return exercised && rumble_result == 0 ? 0 : 2;
}
