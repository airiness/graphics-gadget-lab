#pragma once
#include "Graphics/RHI/RHITextureValidation.h"

#include <d3d12.h>

namespace gglab
{
	[[nodiscard]] RHITextureSupportReason EvaluateD3D12TextureFormatSupport(
		const RHITextureDesc& desc, D3D12_FORMAT_SUPPORT1 support) noexcept;
	[[nodiscard]] RHITextureSupportReason EvaluateD3D12TextureViewFormatSupport(
		const RHITextureDesc& textureDesc, const RHITextureViewDesc& viewDesc,
		D3D12_FORMAT_SUPPORT1 support1, D3D12_FORMAT_SUPPORT2 support2) noexcept;
}
