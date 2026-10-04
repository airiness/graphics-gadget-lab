#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gglab
{
	std::string_view GetFrameCaptureSourceName(FrameCaptureSource source) noexcept
	{
		switch (source)
		{
		case FrameCaptureSource::Scene:
			return "scene";
		case FrameCaptureSource::Composited:
			return "composited";
		}
		GGLAB_UNREACHABLE("Unhandled FrameCaptureSource.");
	}

	std::optional<std::vector<uint8_t>> ConvertFrameCaptureToRgba8(
		const FrameCaptureImage& image) noexcept
	{
		bool swapRedBlue = false;
		switch (image.m_Format)
		{
		case RHIFormat::R8G8B8A8Unorm:
		case RHIFormat::R8G8B8A8UnormSrgb:
			break;
		case RHIFormat::B8G8R8A8Unorm:
		case RHIFormat::B8G8R8A8UnormSrgb:
			swapRedBlue = true;
			break;
		default:
			return std::nullopt;
		}

		const size_t pixelCount = static_cast<size_t>(image.m_Width) * image.m_Height;
		if (pixelCount == 0 || image.m_Pixels.size() != pixelCount * 4)
		{
			return std::nullopt;
		}

		std::vector<uint8_t> rgba(image.m_Pixels.size());
		for (size_t pixel = 0; pixel < pixelCount; ++pixel)
		{
			const uint8_t* source = image.m_Pixels.data() + pixel * 4;
			uint8_t* destination = rgba.data() + pixel * 4;
			destination[0] = swapRedBlue ? source[2] : source[0];
			destination[1] = source[1];
			destination[2] = swapRedBlue ? source[0] : source[2];
			// Display-target alpha is not composited by presentation.
			destination[3] = 0xFF;
		}
		return rgba;
	}
}
