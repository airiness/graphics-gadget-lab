#include "Precompiled.h"
#include "DX12Device.h"

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

	}
	void DX12Device::InitializeDxgiAdapter() noexcept
	{
	}
}