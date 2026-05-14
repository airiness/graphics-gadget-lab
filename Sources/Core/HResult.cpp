#include "Core/Precompiled.h"
#include "Core/HResult.h"
#include "Core/Utility/StringUtils.h"

namespace gglab
{
	namespace
	{
		std::string HexHr(HRESULT hr)
		{
			std::ostringstream os;
			os << "0x" << std::uppercase << std::hex
				<< std::setw(8) << std::setfill('0') << static_cast<unsigned>(hr);
			return os.str();
		}

		std::string NowTimeLocal()
		{
			using namespace std::chrono;
			const auto now = system_clock::now();
			const std::time_t t = system_clock::to_time_t(now);
			std::tm tm{};
			::localtime_s(&tm, &t);
			char buffer[64]{};
			std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec);
			return buffer;
		}

		void OutputBoth(const std::string& str) noexcept
		{
			::OutputDebugStringA(str.c_str());
			::OutputDebugStringA("\n");
			std::fputs(str.c_str(), stderr);
			std::fputc('\n', stderr);
			std::fflush(stderr);
		}

		void AppendAdapterInfo(std::ostringstream& os, ID3D12Device* device)
		{
			if (!device) { return; }

			IDXGIDevice* dxgiDevice = nullptr;
			if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || !dxgiDevice)
			{
				return;
			}

			IDXGIAdapter* adapter = nullptr;
			if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter)
			{
				DXGI_ADAPTER_DESC desc{};
				if (SUCCEEDED(adapter->GetDesc(&desc)))
				{
					os << "Adapter: VendorId=" << desc.VendorId
						<< " DeviceId=" << desc.DeviceId
						<< " SubSysId=" << desc.SubSysId
						<< " Revision=" << desc.Revision
						<< " | Luid=(" << desc.AdapterLuid.HighPart << "," << desc.AdapterLuid.LowPart << ")\n"
						<< "         Desc=\"" << utils::ToString(desc.Description) << "\"\n";
				}
				adapter->Release();
			}
			dxgiDevice->Release();
		}
	}

	std::string FormatHResult(HRESULT hr)
	{
		_com_error e(hr);
		std::string msg = utils::ToString(e.ErrorMessage());
		if (msg.empty())
		{
			return HexHr(hr);
		}
		std::ostringstream os;
		os << HexHr(hr) << " (" << msg << ")";
		return os.str();
	}

	bool IsDeviceRemovedHResult(HRESULT hr) noexcept
	{
		return hr == DXGI_ERROR_DEVICE_REMOVED
			|| hr == DXGI_ERROR_DEVICE_HUNG
			|| hr == DXGI_ERROR_DEVICE_RESET;
	}

	void ReportAndAbort(
		HRESULT hr,
		ID3D12Device* device,
		std::string_view context,
		std::source_location loc) noexcept
	{
		std::ostringstream os;

		os << "=== FATAL: DirectX Failure ===\n";
		os << "Time   : " << NowTimeLocal() << "\n";
		os << "Thread : " << ::GetCurrentThreadId() << "\n";
		os << "Where  : " << loc.file_name() << ":" << loc.line()
			<< " (" << loc.function_name() << ")\n";
		if (!context.empty())
		{
			os << "Expr   : " << context << "\n";
		}

		os << "Result : " << FormatHResult(hr) << "\n";

		if (device && IsDeviceRemovedHResult(hr))
		{
			const HRESULT dr = device->GetDeviceRemovedReason();
			os << "DXGI   : DeviceRemovedReason = " << FormatHResult(dr) << "\n";
			AppendAdapterInfo(os, device);
		}

		os << "Action : Abort immediately.\n"
			"==============================";

		const std::string text = os.str();
		OutputBoth(text);

#if defined(BUILD_DEBUG)
		__debugbreak();
#endif
		std::abort();
	}
}