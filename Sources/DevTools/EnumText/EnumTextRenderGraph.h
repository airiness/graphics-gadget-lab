#pragma once
#include "DevTools/EnumText/EnumText.h"
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"

namespace gglab::devtools
{
	template<>
	struct EnumTextTraits<RGResourceType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGResourceType::RGTexture, "Texture" },
			EnumTextEntry{ RGResourceType::RGBuffer, "Buffer" },
		};
	};

	template<>
	struct EnumTextTraits<RGResourceAccessType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGResourceAccessType::Read, "Read" },
			EnumTextEntry{ RGResourceAccessType::Write, "Write" },
		};
	};

	template<>
	struct EnumTextTraits<RGTextureUsage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGTextureUsage::Sample, "Sample" },
			EnumTextEntry{ RGTextureUsage::RenderTarget, "RTV" },
			EnumTextEntry{ RGTextureUsage::DepthStencil, "DSV Write" },
			EnumTextEntry{ RGTextureUsage::DepthStencilRead, "DSV Read" },
			EnumTextEntry{ RGTextureUsage::UAV, "UAV" },
			EnumTextEntry{ RGTextureUsage::CopySrc, "CopySrc" },
			EnumTextEntry{ RGTextureUsage::CopyDst, "CopyDst" },
			EnumTextEntry{ RGTextureUsage::Present, "Present" },
		};
		static constexpr std::string_view NoneText = "None";
	};

	template<>
	struct EnumTextTraits<RGBufferUsage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGBufferUsage::Vertex, "Vertex" },
			EnumTextEntry{ RGBufferUsage::Index, "Index" },
			EnumTextEntry{ RGBufferUsage::Constant, "Constant" },
			EnumTextEntry{ RGBufferUsage::UAV, "UAV" },
			EnumTextEntry{ RGBufferUsage::CopySrc, "CopySrc" },
			EnumTextEntry{ RGBufferUsage::CopyDst, "CopyDst" },
		};
		static constexpr std::string_view NoneText = "None";
	};

	template<>
	struct EnumTextTraits<RHIAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHIAccess::Common, "Common" },
			EnumTextEntry{ RHIAccess::ShaderResource, "ShaderResource" },
			EnumTextEntry{ RHIAccess::RenderTarget, "RenderTarget" },
			EnumTextEntry{ RHIAccess::DepthStencilRead, "DepthStencilRead" },
			EnumTextEntry{ RHIAccess::DepthStencilWrite, "DepthStencilWrite" },
			EnumTextEntry{ RHIAccess::UnorderedAccess, "UnorderedAccess" },
			EnumTextEntry{ RHIAccess::CopySource, "CopySource" },
			EnumTextEntry{ RHIAccess::CopyDest, "CopyDest" },
			EnumTextEntry{ RHIAccess::VertexBuffer, "VertexBuffer" },
			EnumTextEntry{ RHIAccess::IndexBuffer, "IndexBuffer" },
			EnumTextEntry{ RHIAccess::ConstantBuffer, "ConstantBuffer" },
			EnumTextEntry{ RHIAccess::IndirectArgument, "IndirectArgument" },
			EnumTextEntry{ RHIAccess::Present, "Present" },
		};
		static constexpr std::string_view NoneText = "None";
	};

	template<>
	struct EnumTextTraits<RHILayout>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHILayout::Unknown, "Unknown" },
			EnumTextEntry{ RHILayout::Common, "Common" },
			EnumTextEntry{ RHILayout::ShaderResource, "ShaderResource" },
			EnumTextEntry{ RHILayout::RenderTarget, "RenderTarget" },
			EnumTextEntry{ RHILayout::DepthStencilRead, "DepthStencilRead" },
			EnumTextEntry{ RHILayout::DepthStencilWrite, "DepthStencilWrite" },
			EnumTextEntry{ RHILayout::UnorderedAccess, "UnorderedAccess" },
			EnumTextEntry{ RHILayout::CopySource, "CopySource" },
			EnumTextEntry{ RHILayout::CopyDest, "CopyDest" },
			EnumTextEntry{ RHILayout::Present, "Present" },
		};
	};

	[[nodiscard]] inline std::string BarrierStateText(const RHIResourceState& state)
	{
		return std::format("access={}, layout={}",
			EnumFlagsTextWithBits(state.m_Access),
			EnumValueTextWithBits(state.m_Layout));
	}
}
