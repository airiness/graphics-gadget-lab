#include "Precompiled.h"
#include "DX12RootSignature.h"
#include "DX12Device.h"

namespace graphicsGadgetLab
{
	DX12RootSignature::DX12RootSignature(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device)
	{
	}

	DX12RootSignature::~DX12RootSignature() noexcept
	{
		if (m_RootSignature)
		{
			m_RootSignature->Release();
			m_RootSignature = nullptr;
		}
	}

	void DX12RootSignature::CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc) noexcept
	{
		auto device = m_DX12Device->Get();

		ID3DBlob* signature = nullptr;
		ID3DBlob* error = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
		if (FAILED(hr))
		{
			if (error)
			{
				OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
				error->Release();
			}
			return;
		}
		device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature));
		signature->Release();
	}
} // namespace graphicsGadgetLab
