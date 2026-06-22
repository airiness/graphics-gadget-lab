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
	struct EnumTextTraits<RGStage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGStage::All, "All" },
			EnumTextEntry{ RGStage::AllShading, "AllShading" },
			EnumTextEntry{ RGStage::VertexShading, "VertexShading" },
			EnumTextEntry{ RGStage::IndexInput, "IndexInput" },
			EnumTextEntry{ RGStage::RenderTarget, "RenderTarget" },
			EnumTextEntry{ RGStage::DepthStencil, "DepthStencil" },
			EnumTextEntry{ RGStage::Copy, "Copy" },
		};
		static constexpr std::string_view NoneText = "None";
	};

	template<>
	struct EnumTextTraits<RGAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGAccess::Common, "Common" },
			EnumTextEntry{ RGAccess::ShaderResource, "ShaderResource" },
			EnumTextEntry{ RGAccess::RenderTarget, "RenderTarget" },
			EnumTextEntry{ RGAccess::DepthStencilRead, "DepthStencilRead" },
			EnumTextEntry{ RGAccess::DepthStencilWrite, "DepthStencilWrite" },
			EnumTextEntry{ RGAccess::UnorderedAccess, "UnorderedAccess" },
			EnumTextEntry{ RGAccess::CopySource, "CopySource" },
			EnumTextEntry{ RGAccess::CopyDest, "CopyDest" },
			EnumTextEntry{ RGAccess::VertexBuffer, "VertexBuffer" },
			EnumTextEntry{ RGAccess::IndexBuffer, "IndexBuffer" },
			EnumTextEntry{ RGAccess::ConstantBuffer, "ConstantBuffer" },
		};
		static constexpr std::string_view NoneText = "None";
	};

	template<>
	struct EnumTextTraits<RGLayout>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGLayout::Common, "Common" },
			EnumTextEntry{ RGLayout::ShaderResource, "ShaderResource" },
			EnumTextEntry{ RGLayout::RenderTarget, "RenderTarget" },
			EnumTextEntry{ RGLayout::DepthStencilRead, "DepthStencilRead" },
			EnumTextEntry{ RGLayout::DepthStencilWrite, "DepthStencilWrite" },
			EnumTextEntry{ RGLayout::UnorderedAccess, "UnorderedAccess" },
			EnumTextEntry{ RGLayout::CopySource, "CopySource" },
			EnumTextEntry{ RGLayout::CopyDest, "CopyDest" },
			EnumTextEntry{ RGLayout::Present, "Present" },
		};
	};

	[[nodiscard]] inline std::string BarrierStateText(const RGBarrierState& state)
	{
		return std::format("stage={}, access={}, layout={}",
			EnumFlagsTextWithBits(state.m_Stage),
			EnumFlagsTextWithBits(state.m_Access),
			EnumValueTextWithBits(state.m_Layout));
	}
}
