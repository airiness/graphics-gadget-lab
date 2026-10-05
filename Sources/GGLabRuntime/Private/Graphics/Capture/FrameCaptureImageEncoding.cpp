#include "GGLabRuntime/Graphics/Capture/FrameCaptureImageEncoding.h"
#include "GGLabFoundation/Platform/Win/HResult.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

#include <DirectXTex.h>
#include <combaseapi.h>
#include <wincodec.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gglab
{
	namespace
	{
		// WIC requires COM. CoIncrementMTAUsage keeps the multithreaded
		// apartment alive for the rest of the process: a thread that never
		// initialized COM then uses it implicitly, and the WIC factory that
		// DirectXTex caches process-wide stays valid. The calling thread's own
		// COM state is left alone, so callers never inherit an initialization
		// they would have to balance; a thread in its own apartment keeps it,
		// and WIC encoders work in either apartment.
		[[nodiscard]] bool EnsureComAvailable() noexcept
		{
			static const bool available = []() noexcept
				{
					// The usage is never released: the cached factory lives until exit.
					CO_MTA_USAGE_COOKIE cookie = nullptr;
					return SUCCEEDED(::CoIncrementMTAUsage(&cookie));
				}();
			return available;
		}
	}

	std::optional<std::vector<uint8_t>> EncodeFrameCapturePng(
		const FrameCaptureImage& image) noexcept
	{
		std::optional<std::vector<uint8_t>> rgba = ConvertFrameCaptureToRgba8(image);
		if (!rgba)
		{
			return std::nullopt;
		}

		const DirectX::Image source{
			.width = image.m_Width,
			.height = image.m_Height,
			.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			.rowPitch = static_cast<size_t>(image.m_Width) * 4,
			.slicePitch = rgba->size(),
			.pixels = rgba->data(),
		};

		if (!EnsureComAvailable())
		{
			GGLAB_LOG_GRAPHICS_ERROR("Frame capture PNG encoding could not initialize COM.");
			return std::nullopt;
		}
		DirectX::Blob blob;
		const HRESULT hr = DirectX::SaveToWICMemory(source, DirectX::WIC_FLAGS_NONE,
			DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), blob, &GUID_WICPixelFormat24bppBGR);
		if (FAILED(hr))
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Frame capture PNG encoding failed: {}", FormatHResult(hr));
			return std::nullopt;
		}

		const auto* bytes = static_cast<const uint8_t*>(blob.GetConstBufferPointer());
		return std::vector<uint8_t>(bytes, bytes + blob.GetBufferSize());
	}
}
