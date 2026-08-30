#pragma once

#include "NapaVoxelCore/World/VoxelSample.h"

#include <cstdint>
#include <type_traits>
#include <vector>

namespace napa::voxel
{
	struct Float3
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_Z = 0.0f;

		[[nodiscard]] friend constexpr bool operator==(
			const Float3&, const Float3&) noexcept = default;
	};

	struct FloatAabb
	{
		Float3 m_Min{};
		Float3 m_Max{};

		[[nodiscard]] constexpr bool Contains(Float3 point) const noexcept
		{
			return point.m_X >= m_Min.m_X && point.m_Y >= m_Min.m_Y && point.m_Z >= m_Min.m_Z &&
				point.m_X <= m_Max.m_X && point.m_Y <= m_Max.m_Y && point.m_Z <= m_Max.m_Z;
		}

		[[nodiscard]] friend constexpr bool operator==(
			const FloatAabb&, const FloatAabb&) noexcept = default;
	};

	struct MeshVertex
	{
		// Physical position relative to the owning Chunk origin.
		Float3 m_Position{};
		Float3 m_Normal{};
	};

	struct MeshSection
	{
		VoxelMaterial m_Material = VoxelMaterial::Empty;
		std::vector<std::uint32_t> m_Indices;
	};

	// Vertex positions and bounds use owning Chunk-local physical space.
	struct MeshData
	{
		std::vector<MeshVertex> m_Vertices;
		std::vector<MeshSection> m_Sections;
		FloatAabb m_Bounds{};
	};

	static_assert(std::is_standard_layout_v<Float3>);
	static_assert(std::is_trivially_copyable_v<Float3>);
	static_assert(std::is_standard_layout_v<FloatAabb>);
	static_assert(std::is_trivially_copyable_v<FloatAabb>);
	static_assert(std::is_standard_layout_v<MeshVertex>);
	static_assert(std::is_trivially_copyable_v<MeshVertex>);
}
