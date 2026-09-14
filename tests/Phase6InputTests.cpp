#define SDL_MAIN_HANDLED
#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/subsystems.h"
#include "common/threads.h"
#include "libs/controller.h"
#include "libs/padData.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "PHASE6_INPUT_FAIL %s\n", message); std::exit(1); }
}

#include "Phase6MediaTests.inc"
#include "Phase6ControllerHardware.inc"

int main(int argc, char** argv) {
    Common::InitializeThreads();
    Common::Subsystems subsystems;
    subsystems.Initialize<Config::Lifecycle>();
    Config::ConfigOptions options;
    options.printf_direction = Config::OutputDirection::Silent;
    Config::Load(options);
    subsystems.Initialize<Log::Lifecycle>();
    if (argc == 2 && std::strcmp(argv[1], "--media") == 0) {
        MediaFixture::Run();
        return 0;
    }
    subsystems.Initialize<Libs::Controller::Lifecycle>();
    if (argc == 2 && std::strcmp(argv[1], "--controller") == 0) return ControllerHardware();
    using namespace Libs::Controller;
    PadData data{};
    // Disable the default keyboard fallback to exercise an actual disconnect.
    Disconnect(HOST_INPUT_CONTROLLER_ID);
    Connect(12);
    Check(PadReadState(1, &data) == 0 && data.connected && data.left_stick_x == 128, "connect neutral state");
    SetButton(12, PAD_BUTTON_CROSS, true);
    SetAxis(12, Axis::TriggerLeft, 255);
    SetTouchPad(12, 0, true, 0.5f, 0.5f);
    Check(PadReadState(1, &data) == 0 && data.buttons == (PAD_BUTTON_CROSS | PAD_BUTTON_L2) &&
          data.analog_buttons_l2 == 255 && data.touch_data.touch_num == 1, "recorded input sequence");
    const auto timestamp = data.timestamp;
    const auto count = data.connected_count;
    Disconnect(12);
    Connect(13);
    Check(PadReadState(1, &data) == 0 && data.connected && data.buttons == 0 &&
          data.analog_buttons_l2 == 0 && data.touch_data.touch_num == 0 &&
          data.connected_count == count + 1, "reconnect clears stale input");
    Check(data.timestamp >= timestamp, "reconnect timestamp must not move backwards");
    SetButton(12, PAD_BUTTON_CROSS, true);
    Check(PadReadState(1, &data) == 0 && data.buttons == 0, "retired controller events ignored");
    ResetInputState();
    for (unsigned i = 0; i < 80; ++i) SetAxis(13, Axis::LeftX, i);
    PadData history[64]{};
    Check(PadRead(1, history, 64) == 64, "bounded input history");
    for (unsigned i = 0; i < 64; ++i) {
        Check(history[i].left_stick_x == i + 16, "history retains newest events in order");
        if (i) Check(history[i].timestamp >= history[i-1].timestamp, "ordered event timestamps");
    }
    Check(PadRead(1, history, 0) < 0 && PadRead(1, history, 65) < 0, "invalid counts return guest errors");
    Disconnect(13);
    Disconnect(13); // SDL removal can race a shutdown/disconnect notification.
    Check(PadReadState(1, &data) == 0 && !data.connected, "duplicate removal is harmless");
    std::printf("PHASE6_INPUT_PASS recorded_events=83 reconnects=1\n");
}
