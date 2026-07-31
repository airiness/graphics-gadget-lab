#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12HResult.h"
#include "Core/Platform/Win/Win32DiagnosticOutput.h"
#include "Core/Utility/StringUtils.h"

namespace gglab
{
	namespace
	{
		void AppendAdapterInfo(std::ostringstream& os, ID3D12Device* device)
		{
			if (!device)
			{
				return;
			}

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
					os << "Adapter: VendorId=" << desc.VendorId << " DeviceId=" << desc.DeviceId
						<< " SubSysId=" << desc.SubSysId << " Revision=" << desc.Revision
						<< " | Luid=(" << desc.AdapterLuid.HighPart << ","
						<< desc.AdapterLuid.LowPart << ")\n"
						<< "         Desc=\"" << utils::ToString(desc.Description) << "\"\n";
				}
				adapter->Release();
			}
			dxgiDevice->Release();
		}
	}

	void ReportAndAbortDX12(HRESULT hr, ID3D12Device* device, std::string_view context,
		std::source_location loc) noexcept
	{
		std::ostringstream os;

		os << "=== FATAL: DirectX 12 Failure ===\n";
		os << "Time   : " << win32::FormatLocalTime() << "\n";
		os << "Thread : " << ::GetCurrentThreadId() << "\n";
		os << "Where  : " << loc.file_name() << ":" << loc.line() << " (" << loc.function_name()
			<< ")\n";
		if (!context.empty())
		{
			os << "Expr   : " << context << "\n";
		}

		os << "Result : " << FormatHResult(hr) << "\n";

		if (device && IsDeviceRemovedHResult(hr))
		{
			const HRESULT deviceRemovedReason = device->GetDeviceRemovedReason();
			os << "DXGI   : DeviceRemovedReason = " << FormatHResult(deviceRemovedReason) << "\n";
			AppendAdapterInfo(os, device);
		}

		os << "Action : Abort immediately.\n"
			"==============================";

		const std::string text = os.str();
		win32::WriteDiagnosticOutput(text);

#if defined(BUILD_DEBUG)
		__debugbreak();
#endif
		std::abort();
	}
}
