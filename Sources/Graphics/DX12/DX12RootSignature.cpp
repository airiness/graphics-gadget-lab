#include "Core/Precompiled.h"
#include "Graphics/DX12/DX12RootSignature.h"
#include "Graphics/DX12/DX12Device.h"
#include "Core/HResult.h"

namespace gglab
{
	DX12RootSignature::DX12RootSignature(DX12Device* dx12Device, const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& desc) noexcept :
		m_Desc(desc)
	{
		GGLAB_ASSERT_MSG(dx12Device, "DX12Device is null");

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		GGLAB_HR(D3DX12SerializeVersionedRootSignature(&m_Desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error));

		GGLAB_HR_DX(dx12Device->Get()->CreateRootSignature(
			0,
			signature->GetBufferPointer(),
			signature->GetBufferSize(),
			IID_PPV_ARGS(&m_RootSignature)), dx12Device->Get());
	}
}
