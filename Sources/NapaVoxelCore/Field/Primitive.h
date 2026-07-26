#pragma once

#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/VoxelSample.h"

#include <cstdint>
#include <span>
#include <type_traits>

namespace napa::voxel
{
	struct Double3
	{
		double m_X = 0.0;
		double m_Y = 0.0;
		double m_Z = 0.0;

		[[nodiscard]] friend constexpr bool operator==(
			const Double3&,
			const Double3&) noexcept = default;
	};

	struct PrimitiveStableId
	{
		std::uint64_t m_Value = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const PrimitiveStableId&,
			const PrimitiveStableId&) noexcept = default;
	};

	struct PrimitivePriority
	{
		std::int32_t m_Value = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const PrimitivePriority&,
			const PrimitivePriority&) noexcept = default;
	};

	struct SpherePrimitive
	{
		Double3 m_Center{};
		double m_Radius = 0.0;
	};

	struct AxisAlignedBoxPrimitive
	{
		Double3 m_Center{};
		Double3 m_HalfExtents{};
	};

	struct GroundSlabPrimitive
	{
		Double3 m_Center{};
		Double3 m_HalfExtents{};
	};

	enum class PrimitiveShape : std::uint8_t
	{
		Sphere = 0,
		AxisAlignedBox = 1,
		GroundSlab = 2,
	};

	struct PrimitiveParameters
	{
		SpherePrimitive m_Sphere{};
		AxisAlignedBoxPrimitive m_AxisAlignedBox{};
		GroundSlabPrimitive m_GroundSlab{};
	};

	struct PrimitiveDesc
	{
		PrimitiveStableId m_StableId{};
		PrimitivePriority m_Priority{};
		VoxelMaterial m_Material = VoxelMaterial::Empty;
		PrimitiveShape m_Shape = PrimitiveShape::Sphere;
		PrimitiveParameters m_Parameters{};
	};

	[[nodiscard]] ValidationResult ValidatePrimitive(
		const PrimitiveDesc& primitive) noexcept;
	[[nodiscard]] ValidationResult ValidatePrimitiveSet(
		std::span<const PrimitiveDesc> primitives) noexcept;
	[[nodiscard]] ValidationResult EvaluatePrimitiveSignedDistance(
		const PrimitiveDesc& primitive,
		Double3 position,
		double& signedDistance) noexcept;

	static_assert(std::is_standard_layout_v<Double3>);
	static_assert(std::is_trivially_copyable_v<Double3>);
	static_assert(std::is_standard_layout_v<SpherePrimitive>);
	static_assert(std::is_trivially_copyable_v<SpherePrimitive>);
	static_assert(std::is_standard_layout_v<AxisAlignedBoxPrimitive>);
	static_assert(std::is_trivially_copyable_v<AxisAlignedBoxPrimitive>);
	static_assert(std::is_standard_layout_v<GroundSlabPrimitive>);
	static_assert(std::is_trivially_copyable_v<GroundSlabPrimitive>);
	static_assert(std::is_standard_layout_v<PrimitiveParameters>);
	static_assert(std::is_trivially_copyable_v<PrimitiveParameters>);
	static_assert(std::is_standard_layout_v<PrimitiveDesc>);
	static_assert(std::is_trivially_copyable_v<PrimitiveDesc>);
}
