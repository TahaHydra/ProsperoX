#include "libs/videoDec2Decoder.h"
#include <vector>
#include <algorithm>

namespace MediaFixture {
// Original generated H.264 baseline I_PCM fixture. Syntax follows ITU-T H.264
// sections 7.3.2/7.3.3/7.3.5; no commercial/SDK sample bytes are embedded.
struct Bits {
    std::vector<uint8_t> bytes;
    unsigned used = 0;
    void Bit(unsigned value) {
        if (used % 8 == 0) bytes.push_back(0);
        bytes.back() |= (value & 1u) << (7 - used % 8);
        ++used;
    }
    void Fixed(unsigned value, unsigned count) { while (count) Bit(value >> --count); }
    void Ue(unsigned value) {
        unsigned count = 0;
        for (unsigned v = value + 1; v > 1; v >>= 1) ++count;
        for (unsigned i = 0; i < count; ++i) Bit(0);
        Fixed(value + 1, count + 1);
    }
    void Align() { while (used % 8) Bit(0); }
    void End() { Bit(1); Align(); }
};
static void Nal(std::vector<uint8_t>& stream, uint8_t header, const Bits& bits) {
    stream.insert(stream.end(), {0, 0, 0, 1, header});
    unsigned zeros = 0;
    for (auto byte: bits.bytes) {
        if (zeros >= 2 && byte <= 3) { stream.push_back(3); zeros = 0; }
        stream.push_back(byte);
        zeros = byte == 0 ? zeros + 1 : 0;
    }
}
static std::vector<uint8_t> Frame(uint8_t luma) {
    std::vector<uint8_t> stream;
    Bits sps;
    sps.Fixed(66, 8); sps.Fixed(0, 8); sps.Fixed(10, 8);
    sps.Ue(0); sps.Ue(0); sps.Ue(2); sps.Ue(1); sps.Bit(0);
    sps.Ue(0); sps.Ue(0); sps.Bit(1); sps.Bit(1); sps.Bit(0); sps.Bit(0); sps.End();
    Nal(stream, 0x67, sps);
    Bits pps;
    pps.Ue(0); pps.Ue(0); pps.Bit(0); pps.Bit(0); pps.Ue(0);
    pps.Ue(0); pps.Ue(0); pps.Bit(0); pps.Fixed(0, 2);
    pps.Ue(0); pps.Ue(0); pps.Ue(0); pps.Bit(0); pps.Bit(0); pps.Bit(0); pps.End();
    Nal(stream, 0x68, pps);
    Bits slice;
    slice.Ue(0); slice.Ue(2); slice.Ue(0); slice.Fixed(0, 4); slice.Ue(0);
    slice.Bit(0); slice.Bit(0); slice.Ue(0);
    slice.Ue(25); slice.Align();
    for (unsigned i = 0; i < 256; ++i) slice.Fixed(luma, 8);
    for (unsigned i = 0; i < 64; ++i) slice.Fixed(96, 8);
    for (unsigned i = 0; i < 64; ++i) slice.Fixed(160, 8);
    slice.End(); Nal(stream, 0x65, slice);
    return stream;
}
static void Run() {
    namespace D = Libs::VideoDec2::Decoder;
    Check(D::Create({999, 16, 16}) == nullptr, "unsupported codec rejected");
    auto* decoder = D::Create({1, 16, 16});
    Check(decoder != nullptr, "create generated AVC decoder");
    constexpr size_t size = 256 * 24;
    std::vector<uint8_t> storage(size + 32, 0x5a);
    D::FrameBuffer buffer{storage.data() + 16, size};
    unsigned pictures = 0;
    for (unsigned run = 0; run < 10; ++run) {
        D::Reset(decoder);
        D::PictureInfo old{};
        Check(!D::GetPictureInfo(buffer.data, &old), "reset discards old picture metadata");
        for (unsigned n = 0; n < 3; ++n) {
            const uint8_t luma = 32 + n * 48;
            auto au = Frame(luma);
            D::Input input{au.data(), au.size(), 1000 + n * 3000, 900 + n * 3000, 0x6000 + n};
            D::Output output{};
            Check(D::Decode(decoder, input, buffer, &output) == D::Result::Ok, "generated AU decode");
            Check(output.valid && !output.error_frame && output.width == 16 && output.height == 16 &&
                  output.pitch == 256 && output.buffer_accepted, "decoded geometry and acceptance");
            D::PictureInfo picture{};
            Check(D::GetPictureInfo(buffer.data, &picture) && picture.pts == input.pts &&
                  picture.dts == input.dts && picture.attached_data == input.attached_data,
                  "decode metadata follows frame");
            auto* pixels = static_cast<uint8_t*>(buffer.data);
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x)
                    Check(pixels[y * 256 + x] == luma, "exact generated luma");
            for (unsigned y = 0; y < 8; ++y)
                for (unsigned x = 0; x < 16; ++x)
                    Check(pixels[4096 + y * 256 + x] == (x % 2 ? 160 : 96), "NV12 chroma assignment");
            Check(std::all_of(storage.begin(), storage.begin()+16, [](auto v){return v == 0x5a;}) &&
                  std::all_of(storage.end()-16, storage.end(), [](auto v){return v == 0x5a;}), "output bounds");
            ++pictures;
        }
        D::Output output{};
        Check(D::Flush(decoder, buffer, &output) == D::Result::Ok && !output.valid, "drain has no duplicate frame");
        Check(D::Flush(decoder, buffer, &output) == D::Result::Ok && !output.valid, "repeated drain is stable");
    }
    D::Destroy(decoder);
    D::PictureInfo picture{};
    Check(!D::GetPictureInfo(buffer.data, &picture), "destroy removes metadata");
    std::printf("PHASE6_MEDIA_PASS generated_avc_frames=%u resets=10 exact_nv12=1\n", pictures);
}
}
