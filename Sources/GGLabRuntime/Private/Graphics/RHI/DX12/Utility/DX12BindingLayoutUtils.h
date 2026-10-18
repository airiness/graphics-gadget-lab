#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"

#include <array>
#include <cstdint>
#include <span>
#include <d3dx12.h>

namespace gglab
{
	// Immutable native root-argument validation data; descriptor tables are not root buffers.
	struct DX12RootParameterPlan
	{
		std::array<uint32_t, RHIBindingLayoutDesc::MaxSlots> m_InlineConstantSizesInBytes{};
		std::array<RHIBindingType, RHIBindingLayoutDesc::MaxSlots> m_BufferBindingTypes{};
	};

	[[nodiscard]] DX12RootParameterPlan BuildDX12RootParameterPlan(
		std::span<const D3D12_ROOT_PARAMETER1> parameters) noexcept;

	struct DX12BindingLayoutBuildResult
	{
		std::array<CD3DX12_ROOT_PARAMETER1, RHIBindingLayoutDesc::MaxSlots> m_RootParameters{};
		std::array<CD3DX12_DESCRIPTOR_RANGE1, RHIBindingLayoutDesc::MaxSlots> m_DescriptorRanges{};
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC m_Desc{};
		uint32_t m_RootParameterCount = 0;
		uint32_t m_DescriptorRangeCount = 0;
	};

	void BuildDX12RootSignatureDesc(
		const RHIBindingLayoutDesc& rhiDesc, DX12BindingLayoutBuildResult& outDesc) noexcept;
}
