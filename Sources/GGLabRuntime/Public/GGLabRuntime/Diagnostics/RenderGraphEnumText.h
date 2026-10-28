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
		std::string_view m_Id; // Persisted schema identifier; independent of the UI label.
		std::string_view m_Text;
	};
	template <typename E> struct RGEnumTextTraits;

	template <> struct RGEnumTextTraits<RGPassEncoderType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGPassEncoderType::Graphics, "Graphics", "Graphics" },
			EnumTextEntry{ RGPassEncoderType::Compute, "Compute", "Compute" },
			EnumTextEntry{ RGPassEncoderType::Copy, "Copy", "Copy" },
		};
	};
	template <> struct RGEnumTextTraits<RGResourceType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGResourceType::RGTexture, "Texture", "Texture" },
			EnumTextEntry{ RGResourceType::RGBuffer, "Buffer", "Buffer" },
			EnumTextEntry{ RGResourceType::RGAccelerationStructure, "AccelerationStructure", "AccelerationStructure" },
		};
	};
	template <> struct RGEnumTextTraits<RGOrderingRequirement>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGOrderingRequirement::Ordered, "Ordered", "Ordered" },
			EnumTextEntry{ RGOrderingRequirement::Unordered, "Unordered", "Unordered" },
		};
	};
	template <> struct RGEnumTextTraits<RGDependencyAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGDependencyAccess::Read, "Read", "Read" },
			EnumTextEntry{ RGDependencyAccess::Write, "Write", "Write" },
			EnumTextEntry{ RGDependencyAccess::ReadWrite, "ReadWrite", "ReadWrite" },
		};
	};
	template <> struct RGEnumTextTraits<RGDependencyReason>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGDependencyReason::WriterToReader, "WriterToReader", "Writer -> Reader" },
			EnumTextEntry{ RGDependencyReason::PreviousWriterToWriter, "PreviousWriterToWriter", "Previous Writer -> Writer" },
			EnumTextEntry{ RGDependencyReason::PreviousReaderToWriter, "PreviousReaderToWriter", "Previous Reader -> Writer" },
			EnumTextEntry{ RGDependencyReason::ExportWriterToExport, "ExportWriterToExport", "Export Writer -> Export" },
			EnumTextEntry{ RGDependencyReason::ExportReaderToExport, "ExportReaderToExport", "Export Reader -> Export" },
			EnumTextEntry{ RGDependencyReason::ObservationOrder, "ObservationOrder", "Observation Order" },
		};
	};
	template <> struct RGEnumTextTraits<RGBarrierKind>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGBarrierKind::Transition, "Transition", "Transition" },
			EnumTextEntry{ RGBarrierKind::Uav, "Uav", "UAV" },
			EnumTextEntry{ RGBarrierKind::AccelerationStructure, "AccelerationStructure", "AS Memory" },
		};
	};
	template <> struct RGEnumTextTraits<RGBarrierReason>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGBarrierReason::AccessTransition, "AccessTransition", "Access Transition" },
			EnumTextEntry{ RGBarrierReason::OrderedStorageHazard, "OrderedStorageHazard", "Ordered Storage Hazard" },
			EnumTextEntry{ RGBarrierReason::FinalStateTransition, "FinalStateTransition", "Final State Transition" },
		};
	};
	template <> struct RGEnumTextTraits<RGTextureAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGTextureAccess::None, "None", "None" },
			EnumTextEntry{ RGTextureAccess::Sample, "Sample", "Sample" },
			EnumTextEntry{ RGTextureAccess::RenderTarget, "RenderTarget", "RTV" },
			EnumTextEntry{ RGTextureAccess::DepthStencilWrite, "DepthStencilWrite", "DSV Write" },
			EnumTextEntry{ RGTextureAccess::DepthStencilRead, "DepthStencilRead", "DSV Read" },
			EnumTextEntry{ RGTextureAccess::StorageRead, "StorageRead", "Storage Read" },
			EnumTextEntry{ RGTextureAccess::StorageWrite, "StorageWrite", "Storage Write" },
			EnumTextEntry{ RGTextureAccess::StorageReadWrite, "StorageReadWrite", "Storage ReadWrite" },
			EnumTextEntry{ RGTextureAccess::CopySource, "CopySource", "CopySrc" },
			EnumTextEntry{ RGTextureAccess::CopyDest, "CopyDest", "CopyDst" },
			EnumTextEntry{ RGTextureAccess::Present, "Present", "Present" },
		};
	};
	template <> struct RGEnumTextTraits<RGBufferAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGBufferAccess::None, "None", "None" },
			EnumTextEntry{ RGBufferAccess::Vertex, "Vertex", "Vertex" },
			EnumTextEntry{ RGBufferAccess::Index, "Index", "Index" },
			EnumTextEntry{ RGBufferAccess::Constant, "Constant", "Constant" },
			EnumTextEntry{ RGBufferAccess::StructuredRead, "StructuredRead", "StructuredRead" },
			EnumTextEntry{ RGBufferAccess::StorageRead, "StorageRead", "Storage Read" },
			EnumTextEntry{ RGBufferAccess::StorageWrite, "StorageWrite", "Storage Write" },
			EnumTextEntry{ RGBufferAccess::StorageReadWrite, "StorageReadWrite", "Storage ReadWrite" },
			EnumTextEntry{ RGBufferAccess::CopySource, "CopySource", "CopySrc" },
			EnumTextEntry{ RGBufferAccess::CopyDest, "CopyDest", "CopyDst" },
			EnumTextEntry{ RGBufferAccess::IndirectArgument, "IndirectArgument", "IndirectArgument" },
			EnumTextEntry{ RGBufferAccess::AccelerationStructureInput, "AccelerationStructureInput", "AS Input" },
			EnumTextEntry{ RGBufferAccess::AccelerationStructureScratch, "AccelerationStructureScratch", "AS Scratch" },
		};
	};
	template <> struct RGEnumTextTraits<RHITextureUsage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHITextureUsage::Sampled, "Sampled", "Sampled" },
			EnumTextEntry{ RHITextureUsage::RenderTarget, "RenderTarget", "RenderTarget" },
			EnumTextEntry{ RHITextureUsage::DepthStencil, "DepthStencil", "DepthStencil" },
			EnumTextEntry{ RHITextureUsage::UnorderedAccess, "UnorderedAccess", "UnorderedAccess" },
			EnumTextEntry{ RHITextureUsage::CopySource, "CopySource", "CopySource" },
			EnumTextEntry{ RHITextureUsage::CopyDest, "CopyDest", "CopyDest" },
			EnumTextEntry{ RHITextureUsage::Present, "Present", "Present" },
		};
	};
	template <> struct RGEnumTextTraits<RHIBufferUsage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHIBufferUsage::Vertex, "Vertex", "Vertex" },
			EnumTextEntry{ RHIBufferUsage::Index, "Index", "Index" },
			EnumTextEntry{ RHIBufferUsage::Constant, "Constant", "Constant" },
			EnumTextEntry{ RHIBufferUsage::Structured, "Structured", "Structured" },
			EnumTextEntry{ RHIBufferUsage::UnorderedAccess, "UnorderedAccess", "UnorderedAccess" },
			EnumTextEntry{ RHIBufferUsage::IndirectArgument, "IndirectArgument", "IndirectArgument" },
			EnumTextEntry{ RHIBufferUsage::CopySource, "CopySource", "CopySource" },
			EnumTextEntry{ RHIBufferUsage::CopyDest, "CopyDest", "CopyDest" },
			EnumTextEntry{ RHIBufferUsage::AccelerationStructureInput, "AccelerationStructureInput", "AS Input" },
			EnumTextEntry{ RHIBufferUsage::AccelerationStructureStorage, "AccelerationStructureStorage", "AS Storage" },
			EnumTextEntry{ RHIBufferUsage::AccelerationStructureScratch, "AccelerationStructureScratch", "AS Scratch" },
		};
	};
	template <> struct RGEnumTextTraits<RGAccelerationStructureAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGAccelerationStructureAccess::BuildRead, "BuildRead", "Build Read" },
			EnumTextEntry{ RGAccelerationStructureAccess::BuildWrite, "BuildWrite", "Build Write" },
			EnumTextEntry{ RGAccelerationStructureAccess::QueryRead, "QueryRead", "Query Read" },
		};
	};
	template <> struct RGEnumTextTraits<RHIStage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHIStage::DrawIndirect, "DrawIndirect", "DrawIndirect" },
			EnumTextEntry{ RHIStage::AccelerationStructureBuild, "AccelerationStructureBuild", "AS Build" },
			EnumTextEntry{ RHIStage::IndexInput, "IndexInput", "IndexInput" },
			EnumTextEntry{ RHIStage::VertexShader, "VertexShader", "VertexShader" },
			EnumTextEntry{ RHIStage::PixelShader, "PixelShader", "PixelShader" },
			EnumTextEntry{ RHIStage::ComputeShader, "ComputeShader", "ComputeShader" },
			EnumTextEntry{ RHIStage::RenderTarget, "RenderTarget", "RenderTarget" },
			EnumTextEntry{ RHIStage::DepthStencil, "DepthStencil", "DepthStencil" },
			EnumTextEntry{ RHIStage::Copy, "Copy", "Copy" },
			EnumTextEntry{ RHIStage::Resolve, "Resolve", "Resolve" },
			EnumTextEntry{ RHIStage::Present, "Present", "Present" },
		};
	};
	template <> struct RGEnumTextTraits<RHITextureAspect>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHITextureAspect::Color, "Color", "Color" },
			EnumTextEntry{ RHITextureAspect::Depth, "Depth", "Depth" },
			EnumTextEntry{ RHITextureAspect::Stencil, "Stencil", "Stencil" },
		};
	};
	template <> struct RGEnumTextTraits<RHIAccess>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHIAccess::Common, "Common", "Common" },
			EnumTextEntry{ RHIAccess::ShaderResource, "ShaderResource", "ShaderResource" },
			EnumTextEntry{ RHIAccess::RenderTarget, "RenderTarget", "RenderTarget" },
			EnumTextEntry{ RHIAccess::DepthStencilRead, "DepthStencilRead", "DepthStencilRead" },
			EnumTextEntry{ RHIAccess::DepthStencilWrite, "DepthStencilWrite", "DepthStencilWrite" },
			EnumTextEntry{ RHIAccess::UnorderedAccess, "UnorderedAccess", "UnorderedAccess" },
			EnumTextEntry{ RHIAccess::AccelerationStructureRead, "AccelerationStructureRead", "AS Read" },
			EnumTextEntry{ RHIAccess::AccelerationStructureWrite, "AccelerationStructureWrite", "AS Write" },
			EnumTextEntry{ RHIAccess::AccelerationStructureScratch, "AccelerationStructureScratch", "AS Scratch" },
			EnumTextEntry{ RHIAccess::CopySource, "CopySource", "CopySource" },
			EnumTextEntry{ RHIAccess::CopyDest, "CopyDest", "CopyDest" },
			EnumTextEntry{ RHIAccess::VertexBuffer, "VertexBuffer", "VertexBuffer" },
			EnumTextEntry{ RHIAccess::IndexBuffer, "IndexBuffer", "IndexBuffer" },
			EnumTextEntry{ RHIAccess::ConstantBuffer, "ConstantBuffer", "ConstantBuffer" },
			EnumTextEntry{ RHIAccess::IndirectArgument, "IndirectArgument", "IndirectArgument" },
			EnumTextEntry{ RHIAccess::Present, "Present", "Present" },
		};
	};
	template <> struct RGEnumTextTraits<RHILayout>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RHILayout::Unknown, "Unknown", "Unknown" },
			EnumTextEntry{ RHILayout::Common, "Common", "Common" },
			EnumTextEntry{ RHILayout::ShaderResource, "ShaderResource", "ShaderResource" },
			EnumTextEntry{ RHILayout::RenderTarget, "RenderTarget", "RenderTarget" },
			EnumTextEntry{ RHILayout::DepthStencilRead, "DepthStencilRead", "DepthStencilRead" },
			EnumTextEntry{ RHILayout::DepthStencilWrite, "DepthStencilWrite", "DepthStencilWrite" },
			EnumTextEntry{ RHILayout::UnorderedAccess, "UnorderedAccess", "UnorderedAccess" },
			EnumTextEntry{ RHILayout::CopySource, "CopySource", "CopySource" },
			EnumTextEntry{ RHILayout::CopyDest, "CopyDest", "CopyDest" },
			EnumTextEntry{ RHILayout::Present, "Present", "Present" },
			EnumTextEntry{ RHILayout::Undefined, "Undefined", "Undefined" },
		};
	};

	template <typename E> [[nodiscard]] std::string_view RGEnumId(E value) noexcept
	{
		for (const auto& entry : RGEnumTextTraits<E>::Entries)
		{
			if (entry.m_Value == value)
			{
				return entry.m_Id;
			}
		}
		return "Unknown";
	}

	template <typename E> [[nodiscard]] std::string RGFlagsId(E value)
	{
		auto bits = static_cast<std::underlying_type_t<E>>(value);
		if (bits == 0)
		{
			return "None";
		}
		std::string result;
		for (const auto& entry : RGEnumTextTraits<E>::Entries)
		{
			const auto flag = static_cast<std::underlying_type_t<E>>(entry.m_Value);
			if (flag != 0 && (bits & flag) == flag)
			{
				if (!result.empty())
				{
					result += " | ";
				}
				result += entry.m_Id;
				bits &= ~flag;
			}
		}
		if (bits != 0)
		{
			if (!result.empty())
			{
				result += " | ";
			}
			result += std::format("UnknownBits(0x{:X})", static_cast<uint64_t>(bits));
		}
		return result;
	}
}
