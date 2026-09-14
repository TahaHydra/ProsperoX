#include "../src/libs/libSaveData.cpp"
#include "libs/saveDataMemory.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

static void SaveStoreTest() {
    namespace Store = Libs::SaveData::MemoryStore;
    const auto root = std::filesystem::temp_directory_path() /
        ("prosperox-phase6-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto path = root / "slot.pxm";
    Store::Image original{{1,2,3,4}, {5,6}, {7,8,9}}, next{{10,11}, {12}, {13}}, observed;
    Check(Store::Load(path, &observed) == Store::Result::Missing, "new save slot missing");
    Check(Store::Commit(path, original), "first atomic save");
    Check(Store::Load(path, &observed) == Store::Result::Ok && observed.data == original.data &&
          observed.param == original.param && observed.icon == original.icon, "complete snapshot roundtrip");
    // A crashed writer's temporary file must not replace the committed snapshot.
    const auto temp = std::filesystem::path(path.string() + ".tmp");
    { std::ofstream orphan(temp, std::ios::binary); orphan << "partial"; }
    Check(Store::Load(path, &observed) == Store::Result::Ok && observed.data == original.data,
          "orphan staging file ignored");
    std::filesystem::remove(temp);
    std::filesystem::create_directory(temp); // Deterministic I/O fault at staging creation.
    Check(!Store::Commit(path, next), "I/O failure is reported");
    Check(Store::Load(path, &observed) == Store::Result::Ok && observed.data == original.data,
          "failed write preserves committed save");
    std::filesystem::remove(temp);
#ifdef _WIN32
    const auto locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "lock committed snapshot against replacement");
    Check(!Store::Commit(path, next), "publication failure after successful staging is reported");
    Check(Store::Load(path, &observed) == Store::Result::Ok && observed.data == original.data,
          "failed replacement preserves previous complete snapshot");
    CloseHandle(locked);
#endif
    Check(Store::Commit(path, next), "replacement commit after recovery");
    Check(Store::Load(path, &observed) == Store::Result::Ok && observed.data == next.data && observed.icon == next.icon,
          "replacement published as a complete snapshot");
    { std::fstream corrupt(path, std::ios::in | std::ios::out | std::ios::binary); corrupt.seekp(40); corrupt.put('\xff'); }
    Check(Store::Load(path, &observed) == Store::Result::Corrupt && observed.data == next.data,
          "corrupt snapshot rejected without mutating caller state");
    std::filesystem::remove_all(root);
    std::printf("PHASE6_SAVE_STORE_PASS atomic_replace=1 interrupted_staging=1 io_failure=1 corruption=1\n");
}

static void SaveExportsTest(const char* mode, const char* directory) {
    using namespace Libs::SaveData;
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    Check(SaveDataInitialize3(nullptr) == OK, "save initialize");
    struct SaveDataMemorySetup2 setup{};
    setup.user_id = 1; setup.slot_id = 2; setup.memory_size = 64;
    SaveDataMemorySetupResult result{};
    Check(SaveDataSetupSaveDataMemory2(&setup, &result) == OK, "memory setup");
    uint8_t payload[4] = {0x13, 0x26, 0x39, 0x4c};
    SaveDataMemoryData range{}; range.buf = payload; range.buf_size = sizeof(payload); range.offset = 17;
    if (std::strcmp(mode, "--save-write") == 0) {
        Check(result.existed_memory_size == 0, "first setup reports no existing memory");
        SaveDataMemorySet2 set{}; set.user_id = 1; set.slot_id = 2; set.data_num = 1; set.data = &range;
        Check(SaveDataSetSaveDataMemory2(&set) == OK, "persist memory write");
        const int32_t user = 1;
        Check(SaveDataSyncSaveDataMemory(&user) == OK, "sync persisted memory");
        SaveDataEvent event{};
        Check(SaveDataGetEventResult(nullptr, &event) == OK && event.type == 3 && event.error_code == OK,
              "sync completion event");
        range.offset = 63;
        Check(SaveDataSetSaveDataMemory2(&set) == SAVE_DATA_ERROR_PARAMETER, "out-of-range write rejected");
        uint8_t changed[] = {9,9,9,9};
        SaveDataMemoryData scatter[2]{};
        scatter[0] = range; scatter[0].offset = 17; scatter[0].buf = changed;
        scatter[1] = range;
        set.data = scatter; set.data_num = 2;
        Check(SaveDataSetSaveDataMemory2(&set) == SAVE_DATA_ERROR_PARAMETER, "scatter validation precedes all writes");
        set.data_num = 1;
        auto temporary = std::filesystem::path(memory_path(1, 2) + ".tmp");
        std::filesystem::create_directory(temporary);
        Check(SaveDataSetSaveDataMemory2(&set) == SAVE_DATA_ERROR_INTERNAL, "backend I/O failure reaches save caller");
        std::filesystem::remove(temporary);
        uint8_t observed[4]{};
        range.offset = 17; range.buf = observed;
        SaveDataMemoryGet2 get{}; get.user_id = 1; get.slot_id = 2; get.data = &range;
        Check(SaveDataGetSaveDataMemory2(&get) == OK && std::memcmp(observed, payload, 4) == 0,
              "failed save preserves in-memory committed state");
    } else {
        Check(result.existed_memory_size == 64, "restart reports existing size");
        uint8_t readback[4]{}; range.buf = readback;
        SaveDataMemoryGet2 get{}; get.user_id = 1; get.slot_id = 2; get.data = &range;
        Check(SaveDataGetSaveDataMemory2(&get) == OK && std::memcmp(readback, payload, 4) == 0,
              "save survives a separate process launch");
        get.slot_id = 3;
        Check(SaveDataGetSaveDataMemory2(&get) == SAVE_DATA_ERROR_MEMORY_NOT_READY, "slot isolation");
        get.slot_id = 2; get.user_id = 2;
        Check(SaveDataGetSaveDataMemory2(&get) == SAVE_DATA_ERROR_MEMORY_NOT_READY, "user isolation");
    }
    Check(SaveDataTerminate() == OK, "save terminate");
    std::printf("PHASE6_SAVE_EXPORTS_PASS mode=%s\n", mode);
}
