#include "Graphics/RHI/DX12/DX12RootSignature.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Platform/Win/HResult.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12HResult.h"

#include <span>

namespace gglab
{
	namespace
	{
		[[nodiscard]] std::span<const D3D12_ROOT_PARAMETER1> GetRootParameters(
			const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& desc) noexcept
		{
			// The parameter plan reads the 1.1 layout. Any other version yields an empty plan,
			// which rejects every root buffer and inline-constant binding.
			GGLAB_ASSERT_MSG(desc.Version == D3D_ROOT_SIGNATURE_VERSION_1_1,
				"DX12 root parameter plans require a version 1.1 root signature description.");
			if (desc.Version != D3D_ROOT_SIGNATURE_VERSION_1_1)
			{
				return {};
			}
			return { desc.Desc_1_1.pParameters, desc.Desc_1_1.NumParameters };
		}
	}

	DX12RootSignature::DX12RootSignature(
		DX12Device* dx12Device, const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& desc) noexcept :
		m_Desc(desc), m_ParameterPlan(BuildDX12RootParameterPlan(GetRootParameters(desc)))
	{
		GGLAB_ASSERT_MSG(dx12Device, "DX12Device is null");

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		GGLAB_HR(D3DX12SerializeVersionedRootSignature(
			&m_Desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error));

		GGLAB_HR_DX(dx12Device->Get()->CreateRootSignature(0, signature->GetBufferPointer(),
			signature->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature)),
			dx12Device->Get());
	}
}
