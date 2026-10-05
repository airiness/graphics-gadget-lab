#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gglab
{
	// Encodes a captured display image as an sRGB PNG with opaque 8-bit RGB
	// pixels. Returns nullopt for formats without an 8-bit display conversion or
	// when the platform encoder fails. Safe to call from any thread; it does not
	// change the calling thread's COM initialization.
	[[nodiscard]] std::optional<std::vector<uint8_t>> EncodeFrameCapturePng(
		const FrameCaptureImage& image) noexcept;
}
