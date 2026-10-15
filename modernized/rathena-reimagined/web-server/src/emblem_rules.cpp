#include "rathena/web/emblem_rules.hpp"

#include <cstdint>
#include <cstdlib>

namespace rathena::web {
namespace {

std::uint32_t le32(std::string_view b, std::size_t off) noexcept {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[off])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 3])) << 24;
}

std::uint16_t le16(std::string_view b, std::size_t off) noexcept {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(b[off]) |
                                      static_cast<unsigned char>(b[off + 1]) << 8);
}

bool is_magenta(unsigned char blue, unsigned char green, unsigned char red) noexcept {
    return red == 0xFF && green == 0x00 && blue == 0xFF;
}

constexpr std::size_t kBmpFileHeader = 14;
constexpr std::size_t kBmpInfoHeader = 40;  // BITMAPINFOHEADER

}  // namespace

std::optional<EmblemType> parse_emblem_type(std::string_view s) noexcept {
    if (s == "BMP") return EmblemType::Bmp;
    if (s == "GIF") return EmblemType::Gif;
    return std::nullopt;
}

std::string_view emblem_content_type(EmblemType t) noexcept {
    return t == EmblemType::Gif ? "image/gif" : "image/bmp";
}

std::optional<int> bmp_transparency_percent(std::string_view bmp) noexcept {
    const std::uint64_t len = bmp.size();
    if (len < kBmpFileHeader + kBmpInfoHeader || bmp.substr(0, 2) != "BM") {
        return std::nullopt;
    }
    const std::uint64_t pixel_offset = le32(bmp, 10);
    const std::uint64_t dib_size = le32(bmp, 14);
    const auto width = static_cast<std::int32_t>(le32(bmp, 18));
    const auto height_raw = static_cast<std::int32_t>(le32(bmp, 22));
    const std::uint16_t bpp = le16(bmp, 28);
    const std::uint32_t compression = le32(bmp, 30);

    if (dib_size < kBmpInfoHeader || compression != 0 /* BI_RGB */) return std::nullopt;
    if (bpp != 8 && bpp != 24 && bpp != 32) return std::nullopt;
    if (width <= 0 || height_raw == 0 || height_raw == INT32_MIN) return std::nullopt;

    const std::uint64_t w = static_cast<std::uint64_t>(width);
    const std::uint64_t h = static_cast<std::uint64_t>(std::llabs(static_cast<long long>(height_raw)));
    const std::uint64_t row_size = ((bpp * w + 31) / 32) * 4;
    if (row_size == 0 || h > len || row_size > len) return std::nullopt;
    if (pixel_offset > len || row_size * h > len - pixel_offset) return std::nullopt;

    // 8-bpp: resolve which palette indices are magenta.
    bool magenta_index[256] = {};
    if (bpp == 8) {
        std::uint64_t colors = le32(bmp, 46);
        if (colors == 0 || colors > 256) colors = 256;
        const std::uint64_t palette_off = kBmpFileHeader + dib_size;
        if (palette_off > len || colors * 4 > len - palette_off) return std::nullopt;
        for (std::uint64_t i = 0; i < colors; ++i) {
            const std::size_t p = static_cast<std::size_t>(palette_off + i * 4);
            magenta_index[i] = is_magenta(static_cast<unsigned char>(bmp[p]),
                                          static_cast<unsigned char>(bmp[p + 1]),
                                          static_cast<unsigned char>(bmp[p + 2]));
        }
    }

    std::uint64_t transparent = 0;
    const std::uint64_t bytes_pp = bpp / 8u;
    for (std::uint64_t y = 0; y < h; ++y) {
        const std::uint64_t row = pixel_offset + y * row_size;
        for (std::uint64_t x = 0; x < w; ++x) {
            const std::size_t p = static_cast<std::size_t>(row + x * bytes_pp);
            if (bpp == 8) {
                transparent += magenta_index[static_cast<unsigned char>(bmp[p])] ? 1u : 0u;
            } else {
                transparent += is_magenta(static_cast<unsigned char>(bmp[p]),
                                          static_cast<unsigned char>(bmp[p + 1]),
                                          static_cast<unsigned char>(bmp[p + 2]))
                                   ? 1u
                                   : 0u;
            }
        }
    }
    return static_cast<int>((transparent * 100u) / (w * h));
}

EmblemVerdict validate_emblem(EmblemType type, std::string_view bytes,
                              const EmblemPolicy& policy) noexcept {
    if (bytes.size() > kMaxEmblemSize) {
        return EmblemVerdict::TooLarge;
    }
    if (type == EmblemType::Gif) {
        if (!policy.allow_gifs) return EmblemVerdict::GifDisabled;
        if (bytes.substr(0, 3) != "GIF") return EmblemVerdict::BadMagic;
        // GIF transparency check: legacy TODO; not part of BR-WEB-006 (BMP only).
        return EmblemVerdict::Ok;
    }
    if (bytes.size() < kBmpFileHeader) return EmblemVerdict::TooShort;
    if (bytes.substr(0, 2) != "BM") return EmblemVerdict::BadMagic;
    if (le32(bytes, 2) != bytes.size()) return EmblemVerdict::SizeMismatch;
    if (policy.transparency_limit < 100) {
        const auto percent = bmp_transparency_percent(bytes);
        if (!percent) return EmblemVerdict::UnsupportedBitmap;
        if (*percent > policy.transparency_limit) return EmblemVerdict::TooTransparent;
    }
    return EmblemVerdict::Ok;
}

std::uint32_t next_emblem_version(std::optional<std::uint32_t> current) noexcept {
    return current ? *current + 1 : kEmblemStartVersion;
}

}  // namespace rathena::web
