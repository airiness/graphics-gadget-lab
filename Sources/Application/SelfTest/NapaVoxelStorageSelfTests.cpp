#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/World/VoxelSample.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace gglab
{
	void RunNapaVoxelStorageSelfTests(SelfTestContext& context) noexcept
	{
		using namespace napa::voxel;

		context.Check(IsoValue == 128, "The canonical iso value is 128");
		context.Check(
			std::is_same_v<
				std::underlying_type_t<VoxelMaterial>,
				std::uint8_t>,
			"VoxelMaterial uses uint8_t as its underlying type");
		context.Check(
			std::is_standard_layout_v<VoxelSample>,
			"VoxelSample has standard layout");
		context.Check(
			std::is_trivially_copyable_v<VoxelSample>,
			"VoxelSample is trivially copyable");
		context.Check(
			sizeof(VoxelSample) == 3,
			"VoxelSample occupies exactly three bytes");
		context.Check(
			offsetof(VoxelSample, m_Density) == 0 &&
				offsetof(VoxelSample, m_Material) == 1 &&
				offsetof(VoxelSample, m_Damage) == 2,
			"VoxelSample fields have the canonical byte offsets");

		context.Check(
			DefaultVoxelSample ==
				VoxelSample{
					.m_Density = 0,
					.m_Material = VoxelMaterial::Empty,
					.m_Damage = 0,
				} &&
				ValidateVoxelSample(DefaultVoxelSample).Succeeded(),
			"The default voxel sample is canonical empty");

		const VoxelSample nonCanonicalEmpty{
			.m_Density = IsoValue - 1,
			.m_Material = VoxelMaterial::Stone,
			.m_Damage = 37,
		};
		context.Check(
			ValidateVoxelSample(nonCanonicalEmpty).m_Error ==
				ValidationError::NonCanonicalVoxelSample,
			"Voxel validation detects non-canonical empty data");
		context.Check(
			CanonicalizeVoxelSample(nonCanonicalEmpty) ==
				VoxelSample{
					.m_Density = IsoValue - 1,
					.m_Material = VoxelMaterial::Empty,
					.m_Damage = 0,
				},
			"Empty canonicalization clears material and damage");

		const VoxelSample solid{
			.m_Density = IsoValue,
			.m_Material = VoxelMaterial::Stone,
			.m_Damage = 41,
		};
		context.Check(
			CanonicalizeVoxelSample(solid) == solid &&
				ValidateVoxelSample(solid).Succeeded(),
			"Solid canonicalization preserves material and damage");

		context.Check(
			ValidateVoxelSample(
				{
					.m_Density = IsoValue,
					.m_Material = VoxelMaterial::Empty,
					.m_Damage = 0,
				}).m_Error ==
				ValidationError::NonCanonicalVoxelSample,
			"Solid samples require a non-empty material");
		context.Check(
			ValidateVoxelSample(
				{
					.m_Density = IsoValue,
					.m_Material = static_cast<VoxelMaterial>(255),
					.m_Damage = 0,
				}).m_Error ==
				ValidationError::InvalidVoxelMaterial,
			"Voxel validation rejects unknown materials");
	}
}
