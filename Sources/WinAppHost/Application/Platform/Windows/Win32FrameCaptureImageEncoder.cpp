#include "Application/Platform/Windows/Win32FrameCaptureImageEncoder.h"
#include "AppRuntimeLog.h"
#include "GGLabFoundation/Platform/Win/HResult.h"

#include <windows.h>
#include <objbase.h>
#include <propidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstring>
#include <span>

#pragma comment(lib, "windowscodecs.lib")

namespace gglab::win32
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		// Joins the multithreaded apartment for one call and leaves it again. A
		// thread that already joined an apartment keeps it; WIC works in either.
		class ScopedComInitialization final
		{
		public:
			ScopedComInitialization() noexcept
			{
				const HRESULT result = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				m_Uninitialize = SUCCEEDED(result);
				m_Available = SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
			}
			ScopedComInitialization(const ScopedComInitialization&) = delete;
			ScopedComInitialization& operator=(const ScopedComInitialization&) = delete;
			~ScopedComInitialization() noexcept
			{
				if (m_Uninitialize)
				{
					::CoUninitialize();
				}
			}

			[[nodiscard]] bool IsAvailable() const noexcept { return m_Available; }

		private:
			bool m_Uninitialize = false;
			bool m_Available = false;
		};

		// Every WIC object is created and released within the call, so no COM
		// object outlives the apartment it was created in.
		[[nodiscard]] HRESULT EncodePng(std::span<const uint8_t> bgr, uint32_t width,
			uint32_t height, std::vector<uint8_t>& outPng) noexcept
		{
			ComPtr<IWICImagingFactory> factory;
			HRESULT result = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr,
				CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
			ComPtr<IStream> stream;
			if (SUCCEEDED(result))
			{
				result = ::CreateStreamOnHGlobal(nullptr, TRUE, &stream);
			}
			ComPtr<IWICBitmapEncoder> encoder;
			if (SUCCEEDED(result))
			{
				result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
			}
			if (SUCCEEDED(result))
			{
				result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
			}
			ComPtr<IWICBitmapFrameEncode> frame;
			ComPtr<IPropertyBag2> options;
			if (SUCCEEDED(result))
			{
				result = encoder->CreateNewFrame(&frame, &options);
			}
			if (SUCCEEDED(result))
			{
				result = frame->Initialize(options.Get());
			}
			if (SUCCEEDED(result))
			{
				result = frame->SetSize(width, height);
			}
			WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
			if (SUCCEEDED(result))
			{
				result = frame->SetPixelFormat(&format);
			}
			if (SUCCEEDED(result) && format != GUID_WICPixelFormat24bppBGR)
			{
				result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
			}

			// Display bytes are sRGB-encoded; the sRGB chunk records that.
			ComPtr<IWICMetadataQueryWriter> metadata;
			if (SUCCEEDED(result))
			{
				result = frame->GetMetadataQueryWriter(&metadata);
			}
			if (SUCCEEDED(result))
			{
				PROPVARIANT renderingIntent;
				::PropVariantInit(&renderingIntent);
				renderingIntent.vt = VT_UI1;
				renderingIntent.bVal = 0; // Perceptual.
				result = metadata->SetMetadataByName(L"/sRGB/RenderingIntent", &renderingIntent);
			}

			if (SUCCEEDED(result))
			{
				result = frame->WritePixels(height, width * 3, static_cast<UINT>(bgr.size()),
					const_cast<BYTE*>(bgr.data()));
			}
			if (SUCCEEDED(result))
			{
				result = frame->Commit();
			}
			if (SUCCEEDED(result))
			{
				result = encoder->Commit();
			}

			STATSTG statistics{};
			if (SUCCEEDED(result))
			{
				result = stream->Stat(&statistics, STATFLAG_NONAME);
			}
			HGLOBAL memory = nullptr;
			if (SUCCEEDED(result))
			{
				result = ::GetHGlobalFromStream(stream.Get(), &memory);
			}
			if (FAILED(result))
			{
				return result;
			}
			const void* bytes = ::GlobalLock(memory);
			if (!bytes)
			{
				return HRESULT_FROM_WIN32(::GetLastError());
			}
			outPng.resize(static_cast<size_t>(statistics.cbSize.QuadPart));
			std::memcpy(outPng.data(), bytes, outPng.size());
			::GlobalUnlock(memory);
			return S_OK;
		}
	}

	std::optional<std::vector<uint8_t>> EncodeFrameCapturePng(
		const FrameCaptureImage& image) noexcept
	{
		const std::optional<std::vector<uint8_t>> rgba = ConvertFrameCaptureToRgba8(image);
		if (!rgba)
		{
			return std::nullopt;
		}
		// PNG stores the opaque display color as 24-bit BGR; alpha is dropped.
		const size_t pixelCount = static_cast<size_t>(image.m_Width) * image.m_Height;
		std::vector<uint8_t> bgr(pixelCount * 3);
		for (size_t pixel = 0; pixel < pixelCount; ++pixel)
		{
			bgr[pixel * 3 + 0] = (*rgba)[pixel * 4 + 2];
			bgr[pixel * 3 + 1] = (*rgba)[pixel * 4 + 1];
			bgr[pixel * 3 + 2] = (*rgba)[pixel * 4 + 0];
		}

		const ScopedComInitialization com;
		if (!com.IsAvailable())
		{
			GGLAB_LOG_ERROR_ALWAYS("Frame capture PNG encoding could not initialize COM.");
			return std::nullopt;
		}
		std::vector<uint8_t> png;
		const HRESULT result = EncodePng(bgr, image.m_Width, image.m_Height, png);
		if (FAILED(result))
		{
			GGLAB_LOG_ERROR_ALWAYS("Frame capture PNG encoding failed: {}", FormatHResult(result));
			return std::nullopt;
		}
		return png;
	}
}
