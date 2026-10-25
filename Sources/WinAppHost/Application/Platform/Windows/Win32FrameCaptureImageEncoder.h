#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gglab::win32
{
	// Encodes a captured display image through WIC as a PNG with opaque 8-bit
	// RGB pixels and an sRGB chunk. Returns nullopt for formats without an 8-bit
	// display conversion or when WIC fails. Callable from any thread: a thread
	// without COM is initialized for the call only, and a thread that already
	// joined an apartment keeps it.
	[[nodiscard]] std::optional<std::vector<uint8_t>> EncodeFrameCapturePng(
		const FrameCaptureImage& image) noexcept;
}
