#pragma once
#include "Graphics/RHI/RHITexture.h"

#include <d3d12.h>

namespace gglab
{
	[[nodiscard]] D3D12_RENDER_TARGET_VIEW_DESC BuildD3D12RenderTargetViewDesc(
		const RHITextureViewDesc& desc,
		const D3D12_RESOURCE_DESC& resourceDesc) noexcept;
	[[nodiscard]] D3D12_DEPTH_STENCIL_VIEW_DESC BuildD3D12DepthStencilViewDesc(
		const RHITextureViewDesc& desc,
		const D3D12_RESOURCE_DESC& resourceDesc) noexcept;
	[[nodiscard]] D3D12_SHADER_RESOURCE_VIEW_DESC BuildD3D12ShaderResourceViewDesc(
		const RHITextureViewDesc& desc,
		const D3D12_RESOURCE_DESC& resourceDesc) noexcept;
	[[nodiscard]] D3D12_UNORDERED_ACCESS_VIEW_DESC BuildD3D12UnorderedAccessViewDesc(
		const RHITextureViewDesc& desc,
		const D3D12_RESOURCE_DESC& resourceDesc) noexcept;
}
