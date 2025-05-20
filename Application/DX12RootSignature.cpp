#include "Precompiled.h"
#include "DX12RootSignature.h"
#include "DX12Device.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12RootSignature::DX12RootSignature(DX12Device* dx12Device, const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& desc) noexcept :
		m_DX12Device(dx12Device),
		m_Desc(desc)
	{
		CreateRootSignature();
	}

	DX12RootSignature::~DX12RootSignature() noexcept
	{
		// TODO: Finalize DX12 object safety.
	}

	void DX12RootSignature::CreateRootSignature() noexcept
	{
		auto device = m_DX12Device->Get();

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		utility::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&m_Desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error));

		utility::ThrowIfFailed(device->CreateRootSignature(
			0,
			signature->GetBufferPointer(),
			signature->GetBufferSize(),
			IID_PPV_ARGS(&m_RootSignature)));

		// TODO: debug name

	}
}
