#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Libs::SaveData::MemoryStore {
struct Image {
    std::vector<uint8_t> data;
    std::vector<uint8_t> param;
    std::vector<uint8_t> icon;
};
enum class Result { Ok, Missing, IoError, Corrupt };
// Host storage policy, not a claimed PS5 API limit. Refuse unreasonable or
// corrupt snapshots before allocating; other-host durability remains deferred.
constexpr uint64_t MAX_BYTES = 512ull * 1024 * 1024;
Result Load(const std::filesystem::path& path, Image* image);
bool Commit(const std::filesystem::path& path, const Image& image);
}
