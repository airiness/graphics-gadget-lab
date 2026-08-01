#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12TextureSupportUtils.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] D3D12_FORMAT_SUPPORT1 TextureDimensionSupport(
			RHITextureDimension dimension) noexcept
		{
			switch (dimension)
			{
			case RHITextureDimension::Texture1D:
				return D3D12_FORMAT_SUPPORT1_TEXTURE1D;
			case RHITextureDimension::Texture2D:
				return D3D12_FORMAT_SUPPORT1_TEXTURE2D;
			case RHITextureDimension::Texture3D:
				return D3D12_FORMAT_SUPPORT1_TEXTURE3D;
			}
			return D3D12_FORMAT_SUPPORT1_NONE;
		}

		[[nodiscard]] bool HasFormatSupport1(
			D3D12_FORMAT_SUPPORT1 actual, D3D12_FORMAT_SUPPORT1 required) noexcept
		{
			return (actual & required) == required;
		}
	}

	RHITextureSupportReason EvaluateD3D12TextureFormatSupport(
		const RHITextureDesc& desc, D3D12_FORMAT_SUPPORT1 support) noexcept
	{
		if (!HasFormatSupport1(support, TextureDimensionSupport(desc.m_Dimension)))
		{
			return RHITextureSupportReason::TextureDimensionUnsupported;
		}

		if (GetRHIFormatInfo(desc.m_Format).m_IsTypeless)
		{
			return RHITextureSupportReason::None;
		}
		if (Test(desc.m_Usage, RHITextureUsage::RenderTarget) &&
			!HasFormatSupport1(support, D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
		{
			return RHITextureSupportReason::RenderTargetUnsupported;
		}
		if (Test(desc.m_Usage, RHITextureUsage::DepthStencil) &&
			!HasFormatSupport1(support, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL))
		{
			return RHITextureSupportReason::DepthStencilUnsupported;
		}
		if (Test(desc.m_Usage, RHITextureUsage::UnorderedAccess) &&
			!HasFormatSupport1(support, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW))
		{
			return RHITextureSupportReason::TypedUnorderedAccessUnsupported;
		}
		return RHITextureSupportReason::None;
	}

	RHITextureSupportReason EvaluateD3D12TextureViewFormatSupport(
		const RHITextureDesc& textureDesc, const RHITextureViewDesc& viewDesc,
		D3D12_FORMAT_SUPPORT1 support1, D3D12_FORMAT_SUPPORT2 support2) noexcept
	{
		if (!HasFormatSupport1(support1, TextureDimensionSupport(textureDesc.m_Dimension)))
		{
			return RHITextureSupportReason::TextureDimensionUnsupported;
		}

		switch (viewDesc.m_Type)
		{
		case RHITextureViewType::RenderTarget:
			return HasFormatSupport1(support1, D3D12_FORMAT_SUPPORT1_RENDER_TARGET)
				? RHITextureSupportReason::None
				: RHITextureSupportReason::RenderTargetUnsupported;
		case RHITextureViewType::DepthStencil:
			return HasFormatSupport1(support1, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL)
				? RHITextureSupportReason::None
				: RHITextureSupportReason::DepthStencilUnsupported;
		case RHITextureViewType::ShaderResource:
			return HasFormatSupport1(support1, D3D12_FORMAT_SUPPORT1_SHADER_LOAD) ||
				HasFormatSupport1(support1, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)
				? RHITextureSupportReason::None
				: RHITextureSupportReason::ShaderResourceUnsupported;
		case RHITextureViewType::UnorderedAccess:
			if (!HasFormatSupport1(
				support1, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW))
			{
				return RHITextureSupportReason::TypedUnorderedAccessUnsupported;
			}
			return (support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0
				? RHITextureSupportReason::None
				: RHITextureSupportReason::TypedUnorderedAccessStoreUnsupported;
		}
		return RHITextureSupportReason::FormatSupportQueryFailed;
	}
}
