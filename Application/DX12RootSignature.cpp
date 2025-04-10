#include "Precompiled.h"
#include "DX12RootSignature.h"
#include "DX12Device.h"

namespace graphicsGadgetLab
{
	DX12RootSignature::DX12RootSignature(DX12Device* dx12Device, const D3D12_ROOT_SIGNATURE_DESC& desc) noexcept :
		m_DX12Device(dx12Device),
		m_Desc(desc)
	{
		CreateRootSignature();
	}

	DX12RootSignature::~DX12RootSignature() noexcept
	{
		if (m_RootSignature)
		{
			m_RootSignature->Release();
			m_RootSignature = nullptr;
		}
	}

	void DX12RootSignature::CreateRootSignature() noexcept
	{
		auto device = m_DX12Device->Get();


	}
} // namespace graphicsGadgetLab
