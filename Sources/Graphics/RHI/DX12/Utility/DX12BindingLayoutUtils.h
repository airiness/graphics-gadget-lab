#pragma once
#include "Graphics/RHI/RHIBindingLayout.h"

#include <array>
#include <d3dx12.h>

namespace gglab
{
	struct DX12BindingLayoutBuildResult
	{
		std::array<CD3DX12_ROOT_PARAMETER1, RHIBindingLayoutDesc::MaxSlots> m_RootParameters{};
		std::array<CD3DX12_DESCRIPTOR_RANGE1, RHIBindingLayoutDesc::MaxSlots> m_DescriptorRanges{};
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC m_Desc{};
		uint32_t m_RootParameterCount = 0;
		uint32_t m_DescriptorRangeCount = 0;
	};

	void BuildDX12RootSignatureDesc(
		const RHIBindingLayoutDesc& rhiDesc,
		DX12BindingLayoutBuildResult& outDesc) noexcept;
}
