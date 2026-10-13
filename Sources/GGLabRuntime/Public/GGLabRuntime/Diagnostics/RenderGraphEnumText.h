#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResourceUtils.h"

#include <array>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

namespace gglab::diagnostics
{
	template <typename E> struct EnumTextEntry
	{
		E m_Value;
		std::string_view m_Text;
	};
	template <typename E> struct RGEnumTextTraits;

	template <> struct RGEnumTextTraits<RGPassEncoderType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RGPassEncoderType::Graphics, "Graphics"},
			EnumTextEntry{RGPassEncoderType::Compute, "Compute"},
			EnumTextEntry{RGPassEncoderType::Copy, "Copy"},
		};
	};
	template <> struct RGEnumTextTraits<RGResourceType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RGResourceType::RGTexture, "Texture"},
			EnumTextEntry{RGResourceType::RGBuffer, "Buffer"},
		};
	};
	template <> struct RGEnumTextTraits<RGOrderingRequirement>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RGOrderingRequirement::Ordered, "Ordered"},
			EnumTextEntry{RGOrderingRequirement::Unordered, "Unordered"},
		};
	};
	template <> struct RGEnumTextTraits<RGDependencyAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RGDependencyAccess::Read, "Read"},
			EnumTextEntry{RGDependencyAccess::Write, "Write"},
			EnumTextEntry{RGDependencyAccess::ReadWrite, "ReadWrite"},
		};
	};
	template <> struct RGEnumTextTraits<RGDependencyReason>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RGDependencyReason::WriterToReader, "Writer -> Reader"},
			EnumTextEntry{RGDependencyReason::PreviousWriterToWriter, "Previous Writer -> Writer"},
			EnumTextEntry{RGDependencyReason::PreviousReaderToWriter, "Previous Reader -> Writer"},
			EnumTextEntry{RGDependencyReason::ExportWriterToExport, "Export Writer -> Export"},
			EnumTextEntry{RGDependencyReason::ExportReaderToExport, "Export Reader -> Export"},
		};
	};
	template <> struct RGEnumTextTraits<RGBarrierKind>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RGBarrierKind::Transition, "Transition"},
			EnumTextEntry{RGBarrierKind::Uav, "UAV"},
		};
	};
	template <> struct RGEnumTextTraits<RGBarrierReason>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RGBarrierReason::AccessTransition, "Access Transition"},
			EnumTextEntry{RGBarrierReason::OrderedStorageHazard, "Ordered Storage Hazard"},
			EnumTextEntry{RGBarrierReason::FinalStateTransition, "Final State Transition"},
		};
	};
	template <> struct RGEnumTextTraits<RGTextureAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RGTextureAccess::None, "None"},
			EnumTextEntry{RGTextureAccess::Sample, "Sample"},
			EnumTextEntry{RGTextureAccess::RenderTarget, "RTV"},
			EnumTextEntry{RGTextureAccess::DepthStencilWrite, "DSV Write"},
			EnumTextEntry{RGTextureAccess::DepthStencilRead, "DSV Read"},
			EnumTextEntry{RGTextureAccess::StorageRead, "Storage Read"},
			EnumTextEntry{RGTextureAccess::StorageWrite, "Storage Write"},
			EnumTextEntry{RGTextureAccess::StorageReadWrite, "Storage ReadWrite"},
			EnumTextEntry{RGTextureAccess::CopySource, "CopySrc"},
			EnumTextEntry{RGTextureAccess::CopyDest, "CopyDst"},
			EnumTextEntry{RGTextureAccess::Present, "Present"},
		};
	};
	template <> struct RGEnumTextTraits<RGBufferAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RGBufferAccess::None, "None"},
			EnumTextEntry{RGBufferAccess::Vertex, "Vertex"},
			EnumTextEntry{RGBufferAccess::Index, "Index"},
			EnumTextEntry{RGBufferAccess::Constant, "Constant"},
			EnumTextEntry{RGBufferAccess::StructuredRead, "StructuredRead"},
			EnumTextEntry{RGBufferAccess::StorageRead, "Storage Read"},
			EnumTextEntry{RGBufferAccess::StorageWrite, "Storage Write"},
			EnumTextEntry{RGBufferAccess::StorageReadWrite, "Storage ReadWrite"},
			EnumTextEntry{RGBufferAccess::CopySource, "CopySrc"},
			EnumTextEntry{RGBufferAccess::CopyDest, "CopyDst"},
			EnumTextEntry{RGBufferAccess::IndirectArgument, "IndirectArgument"},
		};
	};
	template <> struct RGEnumTextTraits<RHITextureUsage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHITextureUsage::Sampled, "Sampled"},
			EnumTextEntry{RHITextureUsage::RenderTarget, "RenderTarget"},
			EnumTextEntry{RHITextureUsage::DepthStencil, "DepthStencil"},
			EnumTextEntry{RHITextureUsage::UnorderedAccess, "UnorderedAccess"},
			EnumTextEntry{RHITextureUsage::CopySource, "CopySource"},
			EnumTextEntry{RHITextureUsage::CopyDest, "CopyDest"},
			EnumTextEntry{RHITextureUsage::Present, "Present"},
		};
	};
	template <> struct RGEnumTextTraits<RHIBufferUsage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHIBufferUsage::Vertex, "Vertex"},
			EnumTextEntry{RHIBufferUsage::Index, "Index"},
			EnumTextEntry{RHIBufferUsage::Constant, "Constant"},
			EnumTextEntry{RHIBufferUsage::Structured, "Structured"},
			EnumTextEntry{RHIBufferUsage::UnorderedAccess, "UnorderedAccess"},
			EnumTextEntry{RHIBufferUsage::IndirectArgument, "IndirectArgument"},
			EnumTextEntry{RHIBufferUsage::CopySource, "CopySource"},
			EnumTextEntry{RHIBufferUsage::CopyDest, "CopyDest"},
		};
	};
	template <> struct RGEnumTextTraits<RHIStage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHIStage::DrawIndirect, "DrawIndirect"},
			EnumTextEntry{RHIStage::IndexInput, "IndexInput"},
			EnumTextEntry{RHIStage::VertexShader, "VertexShader"},
			EnumTextEntry{RHIStage::PixelShader, "PixelShader"},
			EnumTextEntry{RHIStage::ComputeShader, "ComputeShader"},
			EnumTextEntry{RHIStage::RenderTarget, "RenderTarget"},
			EnumTextEntry{RHIStage::DepthStencil, "DepthStencil"},
			EnumTextEntry{RHIStage::Copy, "Copy"},
			EnumTextEntry{RHIStage::Resolve, "Resolve"},
			EnumTextEntry{RHIStage::Present, "Present"},
		};
	};
	template <> struct RGEnumTextTraits<RHITextureAspect>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHITextureAspect::Color, "Color"},
			EnumTextEntry{RHITextureAspect::Depth, "Depth"},
			EnumTextEntry{RHITextureAspect::Stencil, "Stencil"},
		};
	};
	template <> struct RGEnumTextTraits<RHIAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHIAccess::Common, "Common"},
			EnumTextEntry{RHIAccess::ShaderResource, "ShaderResource"},
			EnumTextEntry{RHIAccess::RenderTarget, "RenderTarget"},
			EnumTextEntry{RHIAccess::DepthStencilRead, "DepthStencilRead"},
			EnumTextEntry{RHIAccess::DepthStencilWrite, "DepthStencilWrite"},
			EnumTextEntry{RHIAccess::UnorderedAccess, "UnorderedAccess"},
			EnumTextEntry{RHIAccess::CopySource, "CopySource"},
			EnumTextEntry{RHIAccess::CopyDest, "CopyDest"},
			EnumTextEntry{RHIAccess::VertexBuffer, "VertexBuffer"},
			EnumTextEntry{RHIAccess::IndexBuffer, "IndexBuffer"},
			EnumTextEntry{RHIAccess::ConstantBuffer, "ConstantBuffer"},
			EnumTextEntry{RHIAccess::IndirectArgument, "IndirectArgument"},
			EnumTextEntry{RHIAccess::Present, "Present"},
		};
	};
	template <> struct RGEnumTextTraits<RHILayout>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RHILayout::Unknown, "Unknown"},
			EnumTextEntry{RHILayout::Common, "Common"},
			EnumTextEntry{RHILayout::ShaderResource, "ShaderResource"},
			EnumTextEntry{RHILayout::RenderTarget, "RenderTarget"},
			EnumTextEntry{RHILayout::DepthStencilRead, "DepthStencilRead"},
			EnumTextEntry{RHILayout::DepthStencilWrite, "DepthStencilWrite"},
			EnumTextEntry{RHILayout::UnorderedAccess, "UnorderedAccess"},
			EnumTextEntry{RHILayout::CopySource, "CopySource"},
			EnumTextEntry{RHILayout::CopyDest, "CopyDest"},
			EnumTextEntry{RHILayout::Present, "Present"},
			EnumTextEntry{RHILayout::Undefined, "Undefined"},
		};
	};

	template <typename E> [[nodiscard]] std::string_view RGEnumName(E value) noexcept
	{
		for (const auto& entry : RGEnumTextTraits<E>::Entries)
		{
			if (entry.m_Value == value) return entry.m_Text;
		}
		return "Unknown";
	}

	template <typename E> [[nodiscard]] std::string RGFlagsName(E value)
	{
		auto bits = static_cast<std::underlying_type_t<E>>(value);
		if (bits == 0) return "None";
		std::string result;
		for (const auto& entry : RGEnumTextTraits<E>::Entries)
		{
			const auto flag = static_cast<std::underlying_type_t<E>>(entry.m_Value);
			if (flag != 0 && (bits & flag) == flag)
			{
				if (!result.empty()) result += " | ";
				result += entry.m_Text;
				bits &= ~flag;
			}
		}
		if (bits != 0)
		{
			if (!result.empty()) result += " | ";
			result += std::format("UnknownBits(0x{:X})", static_cast<uint64_t>(bits));
		}
		return result;
	}
}
