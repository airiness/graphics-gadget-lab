#include "Application/SelfTest/FrameCaptureImageEncoderSelfTests.h"

#include "Application/Platform/Windows/Win32FrameCaptureImageEncoder.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <windows.h>
#include <objbase.h>
#include <propidl.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

// Platform PNG encoding for frame captures: pixel layout, the sRGB chunk, and
// COM state on threads the encoder does not own.
namespace gglab
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		struct DecodedPng
		{
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			std::vector<uint8_t> m_Bgr;
			bool m_HasSrgbChunk = false;
		};

		// Decodes on the calling thread, which the caller initialized for COM.
		[[nodiscard]] std::optional<DecodedPng> DecodePng(const std::vector<uint8_t>& png) noexcept
		{
			ComPtr<IStream> stream;
			stream.Attach(::SHCreateMemStream(png.data(), static_cast<UINT>(png.size())));
			ComPtr<IWICImagingFactory> factory;
			ComPtr<IWICBitmapDecoder> decoder;
			ComPtr<IWICBitmapFrameDecode> frame;
			WICPixelFormatGUID format{};
			DecodedPng decoded;
			if (!stream ||
				FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
					IID_PPV_ARGS(&factory))) ||
				FAILED(factory->CreateDecoderFromStream(
					stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
				FAILED(decoder->GetFrame(0, &frame)) ||
				FAILED(frame->GetSize(&decoded.m_Width, &decoded.m_Height)) ||
				FAILED(frame->GetPixelFormat(&format)) || format != GUID_WICPixelFormat24bppBGR)
			{
				return std::nullopt;
			}
			const UINT stride = decoded.m_Width * 3;
			decoded.m_Bgr.resize(static_cast<size_t>(stride) * decoded.m_Height);
			if (FAILED(frame->CopyPixels(nullptr, stride,
				static_cast<UINT>(decoded.m_Bgr.size()), decoded.m_Bgr.data())))
			{
				return std::nullopt;
			}
			ComPtr<IWICMetadataQueryReader> metadata;
			PROPVARIANT renderingIntent;
			::PropVariantInit(&renderingIntent);
			decoded.m_HasSrgbChunk = SUCCEEDED(frame->GetMetadataQueryReader(&metadata)) &&
				SUCCEEDED(metadata->GetMetadataByName(L"/sRGB/RenderingIntent", &renderingIntent));
			::PropVariantClear(&renderingIntent);
			return decoded;
		}

		void RunEncodingTests(SelfTestContext& context) noexcept
		{
			const FrameCaptureImage image{
				.m_Format = RHIFormat::B8G8R8A8UnormSrgb,
				.m_Width = 3,
				.m_Height = 2,
				.m_Pixels = { 0, 0, 255, 0, 0, 255, 0, 7, 255, 0, 0, 9, 1, 2, 3, 4, 40, 50, 60, 70,
					200, 100, 50, 0 },
			};

			// A thread that never initialized COM, like the capture writer thread.
			std::optional<std::vector<uint8_t>> png;
			HRESULT afterEncode = E_FAIL;
			std::thread([&]() noexcept
				{
					png = win32::EncodeFrameCapturePng(image);
					// S_OK: the encoder left the thread without COM, as it found it.
					afterEncode = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
					if (SUCCEEDED(afterEncode))
					{
						::CoUninitialize();
					}
				}).join();
			context.Check(png && afterEncode == S_OK,
				"A thread without COM encodes a PNG and is left without COM");

			const HRESULT decodeCom = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			const std::optional<DecodedPng> decoded = png ? DecodePng(*png) : std::nullopt;
			const std::optional<std::vector<uint8_t>> rgba = ConvertFrameCaptureToRgba8(image);
			bool pixelsMatch = decoded && rgba && decoded->m_Width == 3 && decoded->m_Height == 2;
			for (size_t pixel = 0; pixelsMatch && pixel < 6; ++pixel)
			{
				pixelsMatch = decoded->m_Bgr[pixel * 3 + 0] == (*rgba)[pixel * 4 + 2] &&
					decoded->m_Bgr[pixel * 3 + 1] == (*rgba)[pixel * 4 + 1] &&
					decoded->m_Bgr[pixel * 3 + 2] == (*rgba)[pixel * 4 + 0];
			}
			context.Check(pixelsMatch,
				"PNG round trips preserve display bytes and channel order");
			context.Check(decoded && decoded->m_HasSrgbChunk,
				"The PNG records that its pixels are sRGB-encoded");
			if (SUCCEEDED(decodeCom))
			{
				::CoUninitialize();
			}

			// A thread in a single-threaded apartment keeps it.
			std::optional<std::vector<uint8_t>> apartmentPng;
			HRESULT afterApartmentEncode = E_FAIL;
			std::thread([&]() noexcept
				{
					if (FAILED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
					{
						return;
					}
					apartmentPng = win32::EncodeFrameCapturePng(image);
					// S_FALSE: the thread is still in the apartment it joined.
					afterApartmentEncode = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
					if (SUCCEEDED(afterApartmentEncode))
					{
						::CoUninitialize();
					}
					::CoUninitialize();
				}).join();
			context.Check(apartmentPng && afterApartmentEncode == S_FALSE,
				"A thread in a single-threaded apartment encodes and keeps its apartment");

			FrameCaptureImage unsupported = image;
			unsupported.m_Format = RHIFormat::R16G16B16A16Float;
			context.Check(!win32::EncodeFrameCapturePng(unsupported),
				"Formats without an 8-bit display conversion are not encoded");
		}
	}

	void RunFrameCaptureImageEncoderSelfTests(SelfTestContext& context) noexcept
	{
		RunEncodingTests(context);
	}
}
