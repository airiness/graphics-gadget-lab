#pragma once
#include "Graphics/RenderGraph/RGResourceHandle.h"
#include "Graphics/RenderGraph/RGTypes.h"
#include "Graphics/RHI/RHIBuffer.h"

#include <variant>

namespace gglab
{
	struct TransientTextureAllocation;
	struct TransientBufferAllocation;

	enum RGResourceType : uint8_t
	{
		RGTexture,
		RGBuffer,
	};

	// Describes dependency semantics in the render graph.
	// Read      : the pass depends on previous contents.
	// Write     : the pass produces new contents and does not depend on previous contents.
	// ReadWrite : the pass reads previous contents and writes updated contents.
	enum class RGDependencyAccess : uint8_t
	{
		Read,
		Write,
		ReadWrite,
	};

	enum class RGDependencyReason : uint8_t
	{
		WriterToReader,
		PreviousWriterToWriter,
		PreviousReaderToWriter,
		ExportWriterToExport,
		ExportReaderToExport,
	};

	// Describes how a texture is accessed by a pass.
	// This is a single-use semantic, not a bitmask.
	// Resource creation capabilities are inferred by accumulating all pass usages.
	enum class RGTextureAccess : uint8_t
	{
		None,
		Sample,
		RenderTarget,
		DepthStencilWrite,
		DepthStencilRead,
		UnorderedAccess,
		CopySource,
		CopyDest,
		Present,
	};

	enum class RGBufferAccess : uint8_t
	{
		None,
		Vertex,
		Index,
		Constant,
		StructuredRead,
		UnorderedAccess,
		CopySource,
		CopyDest,
		IndirectArgument,
	};

	template<typename RESOURCE>
	struct RGResourceTraits;

	struct RGTextureResource
	{
		using Descriptor = RHITextureDesc;
		using SubresourceDescriptor = RHISubresourceRange;
		using Access = RGTextureAccess;

		static constexpr RGTextureAccess DefaultReadAccess = RGTextureAccess::Sample;
		static constexpr RGTextureAccess DefaultWriteAccess = RGTextureAccess::RenderTarget;
	};
	using RGTextureId = RGResourceId<RGTextureResource>;

	struct RGBufferResource
	{
		using Descriptor = RHIBufferDesc;
		using SubresourceDescriptor = std::monostate;
		using Access = RGBufferAccess;

		static constexpr RGBufferAccess DefaultReadAccess = RGBufferAccess::Vertex;
		static constexpr RGBufferAccess DefaultWriteAccess = RGBufferAccess::UnorderedAccess;
	};
	using RGBufferId = RGResourceId<RGBufferResource>;

	template<>
	struct RGResourceTraits<RGTextureResource>
	{
		using Access = RGTextureAccess;
		using Handle = RHITextureHandle;
		using PhysicalAllocation = TransientTextureAllocation;
		static constexpr RGResourceType ResourceType = RGResourceType::RGTexture;
	};

	template<>
	struct RGResourceTraits<RGBufferResource>
	{
		using Access = RGBufferAccess;
		using Handle = RHIBufferHandle;
		using PhysicalAllocation = TransientBufferAllocation;
		static constexpr RGResourceType ResourceType = RGResourceType::RGBuffer;
	};
}
