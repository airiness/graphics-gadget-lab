#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"

#include <array>
#include <cstdint>
#include <span>
#include <variant>

namespace gglab
{
	enum class RHIAccelerationStructureType : uint8_t
	{
		BottomLevel,
		TopLevel,
	};

	enum class RHIASBuildPreference : uint8_t
	{
		FastTrace,
		FastBuild,
	};

	struct RHIASBuildLimits
	{
		uint64_t m_MaxPrimitiveCount = 0;
		uint64_t m_MaxInstanceCount = 0;
		uint64_t m_ScratchAlignmentInBytes = 0;
	};

	struct RHIASBufferRange
	{
		RHIBufferHandle m_Buffer{};
		uint64_t m_OffsetInBytes = 0;
		uint64_t m_SizeInBytes = 0;
	};

	// One indexed, opaque triangle geometry per BLAS keeps primitive IDs local to
	// its immutable geometry key. Native addresses and AS storage stay backend-owned.
	struct RHIASIndexedTriangles
	{
		RHIASBufferRange m_Vertices{};
		uint32_t m_VertexCount = 0;
		uint32_t m_VertexStrideInBytes = 0;
		RHIFormat m_VertexFormat = RHIFormat::R32G32B32Float;
		RHIASBufferRange m_Indices{};
		uint32_t m_IndexCount = 0;
		RHIFormat m_IndexFormat = RHIFormat::R32Uint;
	};

	enum class RHIASInstanceFlags : uint8_t
	{
		None = 0,
		TriangleCullDisable = 1u << 0,
		TriangleFrontCounterClockwise = 1u << 1,
	};
	GGLAB_ENUM_FLAGS(RHIASInstanceFlags);

	// Logical CPU input, never a native upload layout. Backends must resolve a live
	// BLAS and pack submission-owned native instances, retained through completion.
	struct RHIASInstanceDesc
	{
		RHIAccelerationStructureHandle m_BottomLevel{};
		// Row-major affine 3x4, mapping geometry positions into Runtime world space.
		std::array<float, 12> m_Transform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
		uint32_t m_InstanceIndex = 0;
		uint8_t m_Mask = 0xff;
		RHIASInstanceFlags m_Flags = RHIASInstanceFlags::None;
	};

	// Borrowed for preparation/recording only. No update, refit or serialization mode.
	struct RHIASBuildInputs
	{
		std::variant<RHIASIndexedTriangles, std::span<const RHIASInstanceDesc>> m_Input;
		RHIASBuildPreference m_Preference = RHIASBuildPreference::FastTrace;

		[[nodiscard]] RHIAccelerationStructureType GetType() const noexcept
		{
			return std::holds_alternative<RHIASIndexedTriangles>(m_Input) ?
				RHIAccelerationStructureType::BottomLevel :
				RHIAccelerationStructureType::TopLevel;
		}
	};

	struct RHIASBuildSizes
	{
		uint64_t m_StorageSizeInBytes = 0;
		uint64_t m_ScratchSizeInBytes = 0;
	};

	enum class RHIASValidationError : uint8_t
	{
		None,
		InvalidLimits,
		InvalidBuildPreference,
		InvalidBuffer,
		InvalidBufferRange,
		InvalidMemoryUsage,
		InvalidVertexFormat,
		InvalidVertexLayout,
		InvalidIndexFormat,
		InvalidIndexCount,
		PrimitiveLimitExceeded,
		InvalidInstanceCount,
		InstanceLimitExceeded,
		InvalidBottomLevel,
		InvalidInstanceIndex,
		InvalidInstanceFlags,
		InvalidTransform,
		InvalidBuildSizes,
		InvalidScratchUsage,
		InvalidScratchAlignment,
	};

	struct RHIASValidationResult
	{
		RHIASValidationError m_Error = RHIASValidationError::None;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_Error == RHIASValidationError::None;
		}
	};

	// Descriptors must be resolved from live handles by the backend, not trusted
	// from callers. The asset owner must also prove every index is below vertex count.
	// These CPU checks do not prove native usage, liveness or GPU completion.
	[[nodiscard]] RHIASValidationResult ValidateRHIASIndexedTriangles(
		const RHIASIndexedTriangles& triangles, const RHIBufferDesc& vertices,
		const RHIBufferDesc& indices, const RHIASBuildLimits& limits) noexcept;
	[[nodiscard]] RHIASValidationResult ValidateRHIASInstances(
		std::span<const RHIASInstanceDesc> instances, const RHIASBuildLimits& limits) noexcept;
	[[nodiscard]] RHIASValidationResult ValidateRHIASBuildPreference(
		RHIASBuildPreference preference) noexcept;
	[[nodiscard]] RHIASValidationResult ValidateRHIASScratch(
		const RHIASBufferRange& scratch, const RHIBufferDesc& buffer,
		const RHIASBuildSizes& sizes, const RHIASBuildLimits& limits) noexcept;
}
