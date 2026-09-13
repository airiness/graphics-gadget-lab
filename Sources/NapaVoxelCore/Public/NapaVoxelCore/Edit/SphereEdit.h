#pragma once

#include "NapaVoxelCore/Field/DensityQuantization.h"
#include "NapaVoxelCore/Math/Vector.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <cstdint>
#include <type_traits>

namespace napa::voxel
{
	class VoxelWorld;
	struct VoxelMutationResult;
	struct VoxelSample;

	struct SphereEdit
	{
		Double3 m_CenterWorld{};
		double m_Radius = 0.0;
		double m_Strength = 0.0;
	};

	struct VoxelEditMaterialRules
	{
		std::uint8_t m_DamagePerHit = 128;
		std::uint8_t m_StoneBreakThreshold = 255;
	};

	struct SphereEditRequest
	{
		SphereEdit m_Brush{};
		VoxelEditMaterialRules m_MaterialRules{};
	};

	struct SphereEditSampleEvaluation
	{
		std::uint8_t m_BrushDensity = 0;
		bool m_DensityPathEligible = false;
		bool m_DamagePathEligible = false;

		[[nodiscard]] friend constexpr bool operator==(
			const SphereEditSampleEvaluation&,
			const SphereEditSampleEvaluation&) noexcept = default;
	};

	class SphereEditContext final
	{
	public:
		SphereEditContext() = default;

		[[nodiscard]] bool IsPrepared() const noexcept { return m_IsPrepared; }
		[[nodiscard]] bool HasDensityPath() const noexcept { return m_HasDensityPath; }
		[[nodiscard]] bool HasDamagePath() const noexcept { return m_HasDamagePath; }
		[[nodiscard]] double GetSanitizedStrength() const noexcept
		{
			return m_Request.m_Brush.m_Strength;
		}
		[[nodiscard]] const SampleAabb& GetLogicalSampleBounds() const noexcept
		{
			return m_LogicalSampleBounds;
		}
		[[nodiscard]] const SampleAabb& GetScanBounds() const noexcept
		{
			return m_ScanBounds;
		}

	private:
		friend ValidationResult ApplySphereEdit(VoxelWorld& world,
			const SphereEditRequest& request, VoxelMutationResult& result);
		friend ValidationResult EvaluateSphereEditSampleTransition(
			const SphereEditContext& context, SampleCoord sample,
			VoxelSample before, VoxelSample& after) noexcept;
		friend ValidationResult PrepareSphereEditContext(const VoxelWorldConfig& config,
			const SphereEditRequest& request, SphereEditContext& context) noexcept;
		friend ValidationResult EvaluateSphereEditSample(const SphereEditContext& context,
			SampleCoord sample, SphereEditSampleEvaluation& evaluation) noexcept;

		VoxelWorldConfig m_Config{};
		SphereEditRequest m_Request{};
		DensityQuantizationContext m_DensityQuantization{};
		SampleAabb m_LogicalSampleBounds{};
		SampleAabb m_ScanBounds{};
		bool m_HasDensityPath = false;
		bool m_HasDamagePath = false;
		bool m_IsPrepared = false;
	};

	[[nodiscard]] ValidationResult ValidateEdit(const SphereEditRequest& request) noexcept;
	[[nodiscard]] ValidationResult PrepareSphereEditContext(const VoxelWorldConfig& config,
		const SphereEditRequest& request, SphereEditContext& context) noexcept;
	[[nodiscard]] ValidationResult EvaluateSphereEditSample(const SphereEditContext& context,
		SampleCoord sample, SphereEditSampleEvaluation& evaluation) noexcept;

	static_assert(std::is_standard_layout_v<SphereEdit>);
	static_assert(std::is_trivially_copyable_v<SphereEdit>);
	static_assert(std::is_standard_layout_v<VoxelEditMaterialRules>);
	static_assert(std::is_trivially_copyable_v<VoxelEditMaterialRules>);
	static_assert(std::is_standard_layout_v<SphereEditRequest>);
	static_assert(std::is_trivially_copyable_v<SphereEditRequest>);
}
