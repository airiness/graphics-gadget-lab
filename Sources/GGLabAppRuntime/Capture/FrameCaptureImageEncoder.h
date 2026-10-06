#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace gglab
{
	// Encodes a captured display image as the bytes of a PNG file; nullopt when
	// the image cannot be encoded. The host supplies it with its platform image
	// codec; the capture coordinator calls it on its writer thread.
	using FrameCaptureImageEncoder =
		std::function<std::optional<std::vector<uint8_t>>(const FrameCaptureImage&)>;
}
