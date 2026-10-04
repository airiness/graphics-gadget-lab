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
		// WIC requires COM on the calling thread. DirectXTex caches one
		// process-wide WIC factory, so the apartment is joined once per thread
		// and never left: uninitializing it could release the apartment that
		// keeps the cached factory alive. A thread that already joined another
		// apartment keeps it; WIC encoders work in either apartment.
		[[nodiscard]] bool EnsureComApartment() noexcept
		{
			thread_local const bool joined = []() noexcept
				{
					const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
					return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
				}();
			return joined;
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

		if (!EnsureComApartment())
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
