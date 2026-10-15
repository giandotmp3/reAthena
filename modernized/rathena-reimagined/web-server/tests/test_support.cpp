#include "test_support.hpp"

namespace rathena::web::test {
namespace {

void put32(std::string& b, std::size_t off, std::uint32_t v) {
    b[off] = static_cast<char>(v & 0xFF);
    b[off + 1] = static_cast<char>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<char>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<char>((v >> 24) & 0xFF);
}
void put16(std::string& b, std::size_t off, std::uint16_t v) {
    b[off] = static_cast<char>(v & 0xFF);
    b[off + 1] = static_cast<char>((v >> 8) & 0xFF);
}

std::string header(int width, int height, std::uint16_t bpp, std::uint32_t palette_bytes,
                   std::uint32_t pixel_bytes) {
    const std::uint32_t offset = 14 + 40 + palette_bytes;
    std::string b(offset, '\0');
    b[0] = 'B';
    b[1] = 'M';
    put32(b, 2, offset + pixel_bytes);  // bfSize (patched by callers if padded)
    put32(b, 10, offset);
    put32(b, 14, 40);
    put32(b, 18, static_cast<std::uint32_t>(width));
    put32(b, 22, static_cast<std::uint32_t>(height));
    put16(b, 26, 1);
    put16(b, 28, bpp);
    put32(b, 30, 0);  // BI_RGB
    put32(b, 34, pixel_bytes);
    put32(b, 46, palette_bytes / 4);
    return b;
}

}  // namespace

std::string make_bmp24(int width, int height, int magenta, std::size_t trailing_pad) {
    const std::uint32_t row = static_cast<std::uint32_t>(((24 * width + 31) / 32) * 4);
    const std::uint32_t pixels = row * static_cast<std::uint32_t>(height);
    std::string b = header(width, height, 24, 0, pixels);
    std::string px(pixels, '\0');
    int painted = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width && painted < magenta; ++x, ++painted) {
            const std::size_t p = static_cast<std::size_t>(y) * row + static_cast<std::size_t>(x) * 3;
            px[p] = static_cast<char>(0xFF);      // B
            px[p + 1] = 0;                        // G
            px[p + 2] = static_cast<char>(0xFF);  // R
        }
    }
    b += px;
    b.append(trailing_pad, '\0');
    put32(b, 2, static_cast<std::uint32_t>(b.size()));
    return b;
}

std::string make_bmp8(int width, int height, int magenta) {
    const std::uint32_t row = static_cast<std::uint32_t>(((8 * width + 31) / 32) * 4);
    const std::uint32_t pixels = row * static_cast<std::uint32_t>(height);
    std::string b = header(width, height, 8, 256 * 4, pixels);
    // palette[1] = magenta (B,G,R,0)
    b[54 + 4] = static_cast<char>(0xFF);
    b[54 + 5] = 0;
    b[54 + 6] = static_cast<char>(0xFF);
    std::string px(pixels, '\0');
    int painted = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width && painted < magenta; ++x, ++painted) {
            px[static_cast<std::size_t>(y) * row + static_cast<std::size_t>(x)] = 1;
        }
    }
    b += px;
    put32(b, 2, static_cast<std::uint32_t>(b.size()));
    return b;
}

std::string make_gif(std::size_t size) {
    std::string g = "GIF89a";
    if (size > g.size()) g.append(size - g.size(), '\0');
    return g;
}

}  // namespace rathena::web::test
