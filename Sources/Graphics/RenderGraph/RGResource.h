#pragma once
#include "Graphics/RenderGraph/RGResourceHandle.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHITexture.h"

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

	enum class RGContentValidity : uint8_t
	{
		Undefined,
		Defined,
	};

	enum class RGOrderingRequirement : uint8_t
	{
		Ordered,
		Unordered,
	};

	enum class RGDependencyReason : uint8_t
	{
		// Liveness dependency: the consumer needs contents produced by the writer.
		WriterToReader,

		// Execution hazards: order passes only when both sides remain live.
		PreviousWriterToWriter,
		PreviousReaderToWriter,

		// Exporting preserves the final writer's contents. Prior readers are only
		// ordered before the export transition when they remain live independently.
		ExportWriterToExport,
		ExportReaderToExport,
	};

	enum class RGBarrierKind : uint8_t
	{
		Transition,
		Uav,
	};

	enum class RGBarrierReason : uint8_t
	{
		AccessTransition,
		OrderedStorageHazard,
		FinalStateTransition,
	};

	[[nodiscard]] constexpr inline bool IsRGLivenessDependency(RGDependencyReason reason) noexcept
	{
		return reason == RGDependencyReason::WriterToReader ||
			reason == RGDependencyReason::ExportWriterToExport;
	}

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
		StorageRead,
		StorageWrite,
		StorageReadWrite,
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
		StorageRead,
		StorageWrite,
		StorageReadWrite,
		CopySource,
		CopyDest,
		IndirectArgument,
	};

	[[nodiscard]] constexpr inline bool IsStorageAccess(RGTextureAccess access) noexcept
	{
		return access == RGTextureAccess::StorageRead || access == RGTextureAccess::StorageWrite ||
			access == RGTextureAccess::StorageReadWrite;
	}

	[[nodiscard]] constexpr inline bool IsStorageAccess(RGBufferAccess access) noexcept
	{
		return access == RGBufferAccess::StorageRead || access == RGBufferAccess::StorageWrite ||
			access == RGBufferAccess::StorageReadWrite;
	}

	[[nodiscard]] constexpr inline bool IsRGAccessCompatible(RGTextureAccess access,
		RGDependencyAccess dependencyAccess, RGOrderingRequirement ordering) noexcept
	{
		if (ordering == RGOrderingRequirement::Unordered && !IsStorageAccess(access))
		{
			return false;
		}

		switch (access)
		{
		case RGTextureAccess::None:
		case RGTextureAccess::Present:
			return false;
		case RGTextureAccess::Sample:
		case RGTextureAccess::DepthStencilRead:
		case RGTextureAccess::CopySource:
		case RGTextureAccess::StorageRead:
			return dependencyAccess == RGDependencyAccess::Read;
		case RGTextureAccess::RenderTarget:
		case RGTextureAccess::DepthStencilWrite:
			return dependencyAccess == RGDependencyAccess::Write ||
				dependencyAccess == RGDependencyAccess::ReadWrite;
		case RGTextureAccess::CopyDest:
		case RGTextureAccess::StorageWrite:
			return dependencyAccess == RGDependencyAccess::Write;
		case RGTextureAccess::StorageReadWrite:
			return dependencyAccess == RGDependencyAccess::ReadWrite;
		}
		GGLAB_UNREACHABLE("Unhandled RGTextureAccess.");
	}

	[[nodiscard]] constexpr inline bool IsRGAccessCompatible(RGBufferAccess access,
		RGDependencyAccess dependencyAccess, RGOrderingRequirement ordering) noexcept
	{
		if (ordering == RGOrderingRequirement::Unordered && !IsStorageAccess(access))
		{
			return false;
		}

		switch (access)
		{
		case RGBufferAccess::None:
			return false;
		case RGBufferAccess::Vertex:
		case RGBufferAccess::Index:
		case RGBufferAccess::Constant:
		case RGBufferAccess::StructuredRead:
		case RGBufferAccess::CopySource:
		case RGBufferAccess::IndirectArgument:
		case RGBufferAccess::StorageRead:
			return dependencyAccess == RGDependencyAccess::Read;
		case RGBufferAccess::CopyDest:
		case RGBufferAccess::StorageWrite:
			return dependencyAccess == RGDependencyAccess::Write;
		case RGBufferAccess::StorageReadWrite:
			return dependencyAccess == RGDependencyAccess::ReadWrite;
		}
		GGLAB_UNREACHABLE("Unhandled RGBufferAccess.");
	}

	template <typename RESOURCE> struct RGResourceTraits;

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
		static constexpr RGBufferAccess DefaultWriteAccess = RGBufferAccess::StorageWrite;
	};
	using RGBufferId = RGResourceId<RGBufferResource>;

	template <> struct RGResourceTraits<RGTextureResource>
	{
		using Access = RGTextureAccess;
		using Handle = RHITextureHandle;
		using PhysicalAllocation = TransientTextureAllocation;
		static constexpr RGResourceType ResourceType = RGResourceType::RGTexture;
	};

	template <> struct RGResourceTraits<RGBufferResource>
	{
		using Access = RGBufferAccess;
		using Handle = RHIBufferHandle;
		using PhysicalAllocation = TransientBufferAllocation;
		static constexpr RGResourceType ResourceType = RGResourceType::RGBuffer;
	};
}
