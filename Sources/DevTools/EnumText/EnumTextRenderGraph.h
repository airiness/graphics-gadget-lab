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
	struct EnumTextTraits<RGDependencyAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGDependencyAccess::Read, "Read" },
			EnumTextEntry{ RGDependencyAccess::Write, "Write" },
			EnumTextEntry{ RGDependencyAccess::ReadWrite, "ReadWrite" },
		};
	};

	template<>
	struct EnumTextTraits<RGTextureAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGTextureAccess::None, "None" },
			EnumTextEntry{ RGTextureAccess::Sample, "Sample" },
			EnumTextEntry{ RGTextureAccess::RenderTarget, "RTV" },
			EnumTextEntry{ RGTextureAccess::DepthStencilWrite, "DSV Write" },
			EnumTextEntry{ RGTextureAccess::DepthStencilRead, "DSV Read" },
			EnumTextEntry{ RGTextureAccess::UnorderedAccess, "UAV" },
			EnumTextEntry{ RGTextureAccess::CopySource, "CopySrc" },
			EnumTextEntry{ RGTextureAccess::CopyDest, "CopyDst" },
			EnumTextEntry{ RGTextureAccess::Present, "Present" },
		};
	};

	template<>
	struct EnumTextTraits<RGBufferAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGBufferAccess::None, "None" },
			EnumTextEntry{ RGBufferAccess::Vertex, "Vertex" },
			EnumTextEntry{ RGBufferAccess::Index, "Index" },
			EnumTextEntry{ RGBufferAccess::Constant, "Constant" },
			EnumTextEntry{ RGBufferAccess::StructuredRead, "StructuredRead" },
			EnumTextEntry{ RGBufferAccess::UnorderedAccess, "UAV" },
			EnumTextEntry{ RGBufferAccess::CopySource, "CopySrc" },
			EnumTextEntry{ RGBufferAccess::CopyDest, "CopyDst" },
			EnumTextEntry{ RGBufferAccess::IndirectArgument, "IndirectArgument" },
		};
	};

	template<>
	struct EnumTextTraits<RHITextureUsage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHITextureUsage::Sampled, "Sampled" },
			EnumTextEntry{ RHITextureUsage::RenderTarget, "RenderTarget" },
			EnumTextEntry{ RHITextureUsage::DepthStencil, "DepthStencil" },
			EnumTextEntry{ RHITextureUsage::UnorderedAccess, "UnorderedAccess" },
			EnumTextEntry{ RHITextureUsage::CopySource, "CopySource" },
			EnumTextEntry{ RHITextureUsage::CopyDest, "CopyDest" },
			EnumTextEntry{ RHITextureUsage::Present, "Present" },
		};
		static constexpr std::string_view NoneText = "None";
	};

	template<>
	struct EnumTextTraits<RHIBufferUsage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHIBufferUsage::Vertex, "Vertex" },
			EnumTextEntry{ RHIBufferUsage::Index, "Index" },
			EnumTextEntry{ RHIBufferUsage::Constant, "Constant" },
			EnumTextEntry{ RHIBufferUsage::Structured, "Structured" },
			EnumTextEntry{ RHIBufferUsage::UnorderedAccess, "UnorderedAccess" },
			EnumTextEntry{ RHIBufferUsage::IndirectArgument, "IndirectArgument" },
			EnumTextEntry{ RHIBufferUsage::CopySource, "CopySource" },
			EnumTextEntry{ RHIBufferUsage::CopyDest, "CopyDest" },
		};
		static constexpr std::string_view NoneText = "None";
	};

	template<>
	struct EnumTextTraits<RHIStage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHIStage::DrawIndirect, "DrawIndirect" },
			EnumTextEntry{ RHIStage::VertexInput, "VertexInput" },
			EnumTextEntry{ RHIStage::VertexShader, "VertexShader" },
			EnumTextEntry{ RHIStage::PixelShader, "PixelShader" },
			EnumTextEntry{ RHIStage::ComputeShader, "ComputeShader" },
			EnumTextEntry{ RHIStage::RenderTarget, "RenderTarget" },
			EnumTextEntry{ RHIStage::DepthStencil, "DepthStencil" },
			EnumTextEntry{ RHIStage::Copy, "Copy" },
			EnumTextEntry{ RHIStage::Resolve, "Resolve" },
			EnumTextEntry{ RHIStage::Present, "Present" },
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
		return std::format("stage={}, access={}, layout={}",
			EnumFlagsTextWithBits(state.m_Stages),
			EnumFlagsTextWithBits(state.m_Access),
			EnumValueTextWithBits(state.m_Layout));
	}
}
