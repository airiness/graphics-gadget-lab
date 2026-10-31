#include "GGLabRuntime/Graphics/RHI/RHIAccelerationStructure.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool ValidLimits(const RHIASBuildLimits& limits) noexcept
		{
			return limits.m_MaxPrimitiveCount != 0 && limits.m_MaxInstanceCount != 0 &&
				std::has_single_bit(limits.m_ScratchAlignmentInBytes);
		}

		[[nodiscard]] RHIASValidationResult ValidateRange(
			const RHIASBufferRange& range, const RHIBufferDesc& buffer) noexcept
		{
			if (!range.m_Buffer.IsValid())
			{
				return { RHIASValidationError::InvalidBuffer };
			}
			if (range.m_SizeInBytes == 0 || range.m_OffsetInBytes > buffer.m_SizeInBytes ||
				range.m_SizeInBytes > buffer.m_SizeInBytes - range.m_OffsetInBytes)
			{
				return { RHIASValidationError::InvalidBufferRange };
			}
			if (buffer.m_MemoryUsage != RHIMemoryUsage::GpuOnly ||
				buffer.m_CreateFlags != RHIBufferCreateFlags::None)
			{
				return { RHIASValidationError::InvalidMemoryUsage };
			}
			return {};
		}

		[[nodiscard]] bool ValidTransform(const std::array<float, 12>& transform) noexcept
		{
			if (!std::ranges::all_of(transform, [](float value) { return std::isfinite(value); }))
			{
				return false;
			}
			// Double intermediates avoid overflow for finite float components. Mirrored
			// transforms are legal; only a singular linear part is invalid.
			const auto at = [&transform](uint32_t row, uint32_t column) noexcept
				{
					return static_cast<double>(transform[row * 4 + column]);
				};
			const double determinant =
				at(0, 0) * (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) -
				at(0, 1) * (at(1, 0) * at(2, 2) - at(1, 2) * at(2, 0)) +
				at(0, 2) * (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0));
			return determinant != 0.0;
		}
	}

	RHIASValidationResult ValidateRHIASIndexedTriangles(const RHIASIndexedTriangles& triangles,
		const RHIBufferDesc& vertices, const RHIBufferDesc& indices,
		const RHIASBuildLimits& limits) noexcept
	{
		if (!ValidLimits(limits))
		{
			return { RHIASValidationError::InvalidLimits };
		}
		if (const auto result = ValidateRange(triangles.m_Vertices, vertices); !result.IsValid())
		{
			return result;
		}
		if (const auto result = ValidateRange(triangles.m_Indices, indices); !result.IsValid())
		{
			return result;
		}
		if (triangles.m_VertexFormat != RHIFormat::R32G32B32Float)
		{
			return { RHIASValidationError::InvalidVertexFormat };
		}
		if (triangles.m_VertexCount == 0 || triangles.m_VertexStrideInBytes < 12 ||
			triangles.m_VertexStrideInBytes % 4 != 0 || triangles.m_Vertices.m_OffsetInBytes % 4 != 0)
		{
			return { RHIASValidationError::InvalidVertexLayout };
		}
		const uint64_t vertexBytes =
			static_cast<uint64_t>(triangles.m_VertexCount - 1) * triangles.m_VertexStrideInBytes + 12;
		if (vertexBytes > triangles.m_Vertices.m_SizeInBytes)
		{
			return { RHIASValidationError::InvalidBufferRange };
		}
		if (triangles.m_IndexFormat != RHIFormat::R32Uint)
		{
			return { RHIASValidationError::InvalidIndexFormat };
		}
		if (triangles.m_IndexCount == 0 || triangles.m_IndexCount % 3 != 0 ||
			triangles.m_Indices.m_OffsetInBytes % sizeof(uint32_t) != 0)
		{
			return { RHIASValidationError::InvalidIndexCount };
		}
		if (static_cast<uint64_t>(triangles.m_IndexCount) * sizeof(uint32_t) >
			triangles.m_Indices.m_SizeInBytes)
		{
			return { RHIASValidationError::InvalidBufferRange };
		}
		if (triangles.m_IndexCount / 3 > limits.m_MaxPrimitiveCount)
		{
			return { RHIASValidationError::PrimitiveLimitExceeded };
		}
		return {};
	}

	RHIASValidationResult ValidateRHIASInstances(std::span<const RHIASInstanceDesc> instances,
		const RHIASBuildLimits& limits) noexcept
	{
		if (!ValidLimits(limits))
		{
			return { RHIASValidationError::InvalidLimits };
		}
		if (instances.empty())
		{
			return { RHIASValidationError::InvalidInstanceCount };
		}
		if (instances.size() > limits.m_MaxInstanceCount)
		{
			return { RHIASValidationError::InstanceLimitExceeded };
		}
		constexpr auto knownFlags = RHIASInstanceFlags::TriangleCullDisable |
			RHIASInstanceFlags::TriangleFrontCounterClockwise;
		for (const auto& instance : instances)
		{
			if (!instance.m_BottomLevel.IsValid())
			{
				return { RHIASValidationError::InvalidBottomLevel };
			}
			if (instance.m_InstanceIndex > 0xffffffu)
			{
				return { RHIASValidationError::InvalidInstanceIndex };
			}
			if ((static_cast<uint8_t>(instance.m_Flags) & ~static_cast<uint8_t>(knownFlags)) != 0)
			{
				return { RHIASValidationError::InvalidInstanceFlags };
			}
			if (!ValidTransform(instance.m_Transform))
			{
				return { RHIASValidationError::InvalidTransform };
			}
		}
		return {};
	}

	RHIASValidationResult ValidateRHIASBuildPreference(RHIASBuildPreference preference) noexcept
	{
		if (preference != RHIASBuildPreference::FastTrace &&
			preference != RHIASBuildPreference::FastBuild)
		{
			return { RHIASValidationError::InvalidBuildPreference };
		}
		return {};
	}

	RHIASValidationResult ValidateRHIASScratch(const RHIASBufferRange& scratch,
		const RHIBufferDesc& buffer, const RHIASBuildSizes& sizes,
		const RHIASBuildLimits& limits) noexcept
	{
		if (!ValidLimits(limits))
		{
			return { RHIASValidationError::InvalidLimits };
		}
		if (const auto result = ValidateRange(scratch, buffer); !result.IsValid())
		{
			return result;
		}
		if (sizes.m_StorageSizeInBytes == 0 || sizes.m_ScratchSizeInBytes == 0 ||
			sizes.m_ScratchSizeInBytes > scratch.m_SizeInBytes)
		{
			return { RHIASValidationError::InvalidBuildSizes };
		}
		if (!Test(buffer.m_Usage, RHIBufferUsage::UnorderedAccess))
		{
			return { RHIASValidationError::InvalidScratchUsage };
		}
		if (scratch.m_OffsetInBytes % limits.m_ScratchAlignmentInBytes != 0)
		{
			return { RHIASValidationError::InvalidScratchAlignment };
		}
		return {};
	}
}
