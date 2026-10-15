// rathena-web-server: guild emblem rules (BR-WEB-005, 006, 007, 008).
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "rathena/web/domain.hpp"
#include "rathena/web/types.hpp"

namespace rathena::web {

enum class EmblemVerdict {
    Ok,
    GifDisabled,        ///< allow_gifs = no
    TooLarge,           ///< > kMaxEmblemSize bytes (BR-WEB-005)
    TooShort,           ///< BMP shorter than its 14-byte file header
    BadMagic,           ///< "BM" / "GIF" signature missing
    SizeMismatch,       ///< BMP bfSize != payload length
    UnsupportedBitmap,  ///< pixel data not parseable while the transparency limit is active
    TooTransparent,     ///< BR-WEB-006
};

struct EmblemPolicy {
    bool allow_gifs{true};
    /// Percent of magenta (FF00FF) pixels allowed; >= 100 disables the check.
    /// BR-WEB-006 [DEFECT]: legacy parsed this key into the wrong variable, so
    /// the limit never applied. The rebuild applies it.
    int transparency_limit{kDefaultEmblemTransparencyLimit};
};

/// "BMP" -> Bmp, "GIF" -> Gif, otherwise nullopt (case-sensitive, as legacy).
std::optional<EmblemType> parse_emblem_type(std::string_view s) noexcept;

/// "image/bmp" or "image/gif" (BR-WEB-008 download content type).
std::string_view emblem_content_type(EmblemType t) noexcept;

/// Percentage (0..100, floor) of pixels whose colour is pure magenta, for
/// uncompressed 8/24/32-bpp BMPs. nullopt if the bitmap cannot be parsed safely.
std::optional<int> bmp_transparency_percent(std::string_view bmp) noexcept;

/// BR-WEB-005 / BR-WEB-006: full upload validation, legacy check order.
EmblemVerdict validate_emblem(EmblemType type, std::string_view bytes,
                              const EmblemPolicy& policy) noexcept;

/// BR-WEB-007: version starts at kEmblemStartVersion and increments per upload.
std::uint32_t next_emblem_version(std::optional<std::uint32_t> current) noexcept;

}  // namespace rathena::web
