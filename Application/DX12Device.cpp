#include "Precompiled.h"
#include "DX12Device.h"
#include "Application.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12Device::DX12Device() noexcept
	{
	}
	DX12Device::~DX12Device() noexcept
	{
	}
	void DX12Device::Initialize() noexcept
	{
	}
	void DX12Device::Update() noexcept
	{
	}
	void DX12Device::Finalize() noexcept
	{
	}
	void DX12Device::InitializeDxgiFactory() noexcept
	{
		ComPtr<IDXGIFactory> dxgiFactory;
		UINT createFactoryFlags = 0;

#ifdef BUILD_DEBUG
		createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
		utility::ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

		// Disabling Alt+Enter
		auto* application = Application::Get();
		auto hWnd = application->GetHwnd();
		dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

        if (FAILED(dxgiFactory.As(&m_DxgiFactory)))
        {
            ASSERT_MSG(false, "DxgiFactory failed.");
        }
	}

	void DX12Device::InitializeDxgiAdapter() noexcept
	{
		ComPtr<IDXGIAdapter> dxgiAdapter;
		utility::ThrowIfFailed(m_DxgiFactory->EnumAdapters(0, &dxgiAdapter));
		dxgiAdapter.As(&m_DxgiAdapter);
		utility::ThrowIfFailed(D3D12CreateDevice(m_DxgiAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_D3D12Device)));
	}
}