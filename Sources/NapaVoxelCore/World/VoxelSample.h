#pragma once

#include "NapaVoxelCore/Validation/ValidationResult.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace napa::voxel
{
	inline constexpr std::uint8_t IsoValue = 128;

	enum class VoxelMaterial : std::uint8_t
	{
		Empty = 0,
		Soil = 1,
		Stone = 2,
	};

	struct VoxelSample
	{
		std::uint8_t m_Density = 0;
		VoxelMaterial m_Material = VoxelMaterial::Empty;
		std::uint8_t m_Damage = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const VoxelSample&,
			const VoxelSample&) noexcept = default;
	};

	inline constexpr VoxelSample DefaultVoxelSample{};

	[[nodiscard]] constexpr bool IsKnownVoxelMaterial(
		VoxelMaterial material) noexcept
	{
		return
			material == VoxelMaterial::Empty ||
			material == VoxelMaterial::Soil ||
			material == VoxelMaterial::Stone;
	}

	[[nodiscard]] constexpr ValidationResult ValidateVoxelSample(
		VoxelSample sample) noexcept
	{
		if (!IsKnownVoxelMaterial(sample.m_Material))
		{
			return { ValidationError::InvalidVoxelMaterial };
		}

		if (sample.m_Density < IsoValue)
		{
			if (sample.m_Material != VoxelMaterial::Empty ||
				sample.m_Damage != 0)
			{
				return { ValidationError::NonCanonicalVoxelSample };
			}
		}
		else if (sample.m_Material == VoxelMaterial::Empty)
		{
			return { ValidationError::NonCanonicalVoxelSample };
		}
		return {};
	}

	[[nodiscard]] constexpr ValidationResult PrepareVoxelSampleForStorage(
		VoxelSample input,
		VoxelSample& output) noexcept
	{
		if (!IsKnownVoxelMaterial(input.m_Material))
		{
			return { ValidationError::InvalidVoxelMaterial };
		}

		if (input.m_Density < IsoValue)
		{
			output = {
				.m_Density = input.m_Density,
				.m_Material = VoxelMaterial::Empty,
				.m_Damage = 0,
			};
			return {};
		}

		if (input.m_Material == VoxelMaterial::Empty)
		{
			return { ValidationError::NonCanonicalVoxelSample };
		}

		output = input;
		return {};
	}

	static_assert(std::is_standard_layout_v<VoxelSample>);
	static_assert(std::is_trivially_copyable_v<VoxelSample>);
	static_assert(sizeof(VoxelSample) == 3);
	static_assert(offsetof(VoxelSample, m_Density) == 0);
	static_assert(offsetof(VoxelSample, m_Material) == 1);
	static_assert(offsetof(VoxelSample, m_Damage) == 2);
	static_assert(
		std::is_same_v<
			std::underlying_type_t<VoxelMaterial>,
			std::uint8_t>);
}
