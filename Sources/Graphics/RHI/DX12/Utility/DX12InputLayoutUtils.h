#pragma once
#include "Graphics/RHI/RHIPipeline.h"

#include <array>
#include <d3d12.h>

namespace gglab
{
	struct DX12InputLayoutBuildResult
	{
		std::array<D3D12_INPUT_ELEMENT_DESC, RHIVertexInputLayoutDesc::MaxAttributes> m_Elements{};
		D3D12_INPUT_LAYOUT_DESC m_Desc{};
		uint32_t m_ElementCount = 0;
	};

	void BuildDX12InputLayoutDesc(
		const RHIVertexInputLayoutDesc& rhiDesc,
		DX12InputLayoutBuildResult& outDesc) noexcept;
}
