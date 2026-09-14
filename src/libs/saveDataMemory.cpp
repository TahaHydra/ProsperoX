#include "libs/saveDataMemory.h"
#include "common/file.h"
#include "xxhash.h"
#include <array>
#include <cstring>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace Libs::SaveData::MemoryStore {
namespace {
constexpr uint64_t MAGIC = 0x31564d535850ull; // PXSMV1, private emulator format.
constexpr size_t HEADER = 40;
void Put64(uint8_t* data, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) data[i] = static_cast<uint8_t>(value >> (8*i));
}
uint64_t Get64(const uint8_t* data) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= uint64_t(data[i]) << (8*i);
    return value;
}
}
Result Load(const std::filesystem::path& path, Image* image) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) return error ? Result::IoError : Result::Missing;
    Common::File file(path, Common::File::Mode::Read);
    if (file.IsInvalid()) return Result::IoError;
    const auto size = file.Size();
    if (size < HEADER || size > MAX_BYTES + HEADER) return Result::Corrupt;
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    uint32_t read = 0;
    file.Read(bytes.data(), static_cast<uint32_t>(size), &read);
    if (read != size) return Result::IoError;
    const auto data_size = Get64(bytes.data()+8), param_size = Get64(bytes.data()+16), icon_size = Get64(bytes.data()+24);
    if (Get64(bytes.data()) != MAGIC || data_size > MAX_BYTES || param_size > MAX_BYTES || icon_size > MAX_BYTES ||
        data_size + param_size + icon_size != size - HEADER ||
        Get64(bytes.data()+32) != XXH3_64bits(bytes.data()+HEADER, bytes.size()-HEADER)) return Result::Corrupt;
    Image result;
    auto begin = bytes.begin() + HEADER;
    result.data.assign(begin, begin + data_size); begin += data_size;
    result.param.assign(begin, begin + param_size); begin += param_size;
    result.icon.assign(begin, bytes.end());
    *image = std::move(result);
    return Result::Ok;
}
bool Commit(const std::filesystem::path& path, const Image& image) {
    const uint64_t size = image.data.size() + uint64_t(image.param.size()) + image.icon.size();
    if (size > MAX_BYTES) return false;
    std::vector<uint8_t> bytes(HEADER + static_cast<size_t>(size));
    Put64(bytes.data(), MAGIC); Put64(bytes.data()+8, image.data.size());
    Put64(bytes.data()+16, image.param.size()); Put64(bytes.data()+24, image.icon.size());
    size_t offset = HEADER;
    for (const auto* part: {&image.data, &image.param, &image.icon}) {
        if (!part->empty()) std::memcpy(bytes.data()+offset, part->data(), part->size());
        offset += part->size();
    }
    Put64(bytes.data()+32, XXH3_64bits(bytes.data()+HEADER, bytes.size()-HEADER));
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    // The caller serializes a slot. Stage in the same directory/volume, flush
    // before publication, and keep the old committed file if replacement fails.
    auto temporary = path; temporary += ".tmp";
    Common::File file;
    if (!file.Create(temporary)) return false;
    uint32_t written = 0;
    file.Write(bytes.data(), static_cast<uint32_t>(bytes.size()), &written);
    const bool flushed = written == bytes.size() && file.Flush();
    file.Close();
    if (!flushed) return false;
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    // No unverified durability promise on deferred hosts.
    return false;
#endif
}
}
