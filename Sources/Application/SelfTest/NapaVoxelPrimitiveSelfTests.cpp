#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/Field/Primitive.h"

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::PrimitiveDesc MakeSphere(
			std::uint64_t stableId,
			napa::voxel::PrimitivePriority priority = {},
			napa::voxel::VoxelMaterial material =
				napa::voxel::VoxelMaterial::Stone) noexcept
		{
			return {
				.m_StableId = { stableId },
				.m_Priority = priority,
				.m_Material = material,
				.m_Shape = napa::voxel::PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = { 1.0, 2.0, 3.0 },
						.m_Radius = 2.0,
					},
				},
			};
		}

		[[nodiscard]] napa::voxel::PrimitiveDesc MakeBox(
			std::uint64_t stableId) noexcept
		{
			return {
				.m_StableId = { stableId },
				.m_Priority = {},
				.m_Material = napa::voxel::VoxelMaterial::Soil,
				.m_Shape =
					napa::voxel::PrimitiveShape::AxisAlignedBox,
				.m_Parameters = {
					.m_AxisAlignedBox = {
						.m_Center = {},
						.m_HalfExtents = { 1.0, 2.0, 3.0 },
					},
				},
			};
		}

		[[nodiscard]] napa::voxel::PrimitiveDesc MakeGroundSlab(
			std::uint64_t stableId) noexcept
		{
			return {
				.m_StableId = { stableId },
				.m_Priority = {},
				.m_Material = napa::voxel::VoxelMaterial::Stone,
				.m_Shape = napa::voxel::PrimitiveShape::GroundSlab,
				.m_Parameters = {
					.m_GroundSlab = {
						.m_Center = { 0.0, -2.0, 0.0 },
						.m_HalfExtents = { 4.0, 0.5, 5.0 },
					},
				},
			};
		}

		[[nodiscard]] bool HasSignedDistance(
			const napa::voxel::PrimitiveDesc& primitive,
			napa::voxel::Double3 position,
			double expected) noexcept
		{
			double distance = 1000.0;
			return
				napa::voxel::EvaluatePrimitiveSignedDistance(
					primitive,
					position,
					distance).Succeeded() &&
				distance == expected;
		}

		void RunPrimitiveLayoutTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			context.Check(
				std::is_standard_layout_v<Double3> &&
					std::is_trivially_copyable_v<Double3> &&
					std::is_standard_layout_v<SpherePrimitive> &&
					std::is_trivially_copyable_v<SpherePrimitive> &&
					std::is_standard_layout_v<
						AxisAlignedBoxPrimitive> &&
					std::is_trivially_copyable_v<
						AxisAlignedBoxPrimitive> &&
					std::is_standard_layout_v<GroundSlabPrimitive> &&
					std::is_trivially_copyable_v<GroundSlabPrimitive> &&
					std::is_standard_layout_v<PrimitiveDesc> &&
					std::is_trivially_copyable_v<PrimitiveDesc>,
				"Primitive descriptors use portable value layouts");
			context.Check(
				std::is_same_v<
					std::underlying_type_t<PrimitiveShape>,
					std::uint8_t>,
				"PrimitiveShape uses uint8_t as its underlying type");
		}

		void RunPrimitiveValidationTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const PrimitiveDesc sphere = MakeSphere(1);
			const PrimitiveDesc box = MakeBox(2);
			const PrimitiveDesc slab = MakeGroundSlab(3);
			context.Check(
				ValidatePrimitive(sphere).Succeeded() &&
					ValidatePrimitive(box).Succeeded() &&
					ValidatePrimitive(slab).Succeeded(),
				"Supported field primitives pass validation");

			PrimitiveDesc invalid = sphere;
			invalid.m_Parameters.m_Sphere.m_Center.m_X =
				std::numeric_limits<double>::infinity();
			context.Check(
				ValidatePrimitive(invalid).m_Error ==
					ValidationError::NonFinitePrimitivePosition,
				"Primitive validation rejects non-finite positions");

			invalid = sphere;
			invalid.m_Parameters.m_Sphere.m_Radius =
				std::numeric_limits<double>::quiet_NaN();
			context.Check(
				ValidatePrimitive(invalid).m_Error ==
					ValidationError::NonFinitePrimitiveSize,
				"Primitive validation rejects non-finite sizes");

			invalid = sphere;
			invalid.m_Parameters.m_Sphere.m_Radius = 0.0;
			context.Check(
				ValidatePrimitive(invalid).m_Error ==
					ValidationError::NonPositiveSphereRadius,
				"Primitive validation rejects a non-positive sphere radius");

			invalid = box;
			invalid.m_Parameters.m_AxisAlignedBox.m_HalfExtents.m_X =
				std::numeric_limits<double>::infinity();
			context.Check(
				ValidatePrimitive(invalid).m_Error ==
					ValidationError::NonFinitePrimitiveSize,
				"Box validation rejects non-finite extents");

			invalid = box;
			invalid.m_Parameters.m_AxisAlignedBox.m_HalfExtents.m_Y =
				-1.0;
			context.Check(
				ValidatePrimitive(invalid).m_Error ==
					ValidationError::NonPositivePrimitiveExtent,
				"Primitive validation rejects a non-positive box extent");

			invalid = slab;
			invalid.m_Parameters.m_GroundSlab.m_Center.m_Z =
				std::numeric_limits<double>::quiet_NaN();
			context.Check(
				ValidatePrimitive(invalid).m_Error ==
					ValidationError::NonFinitePrimitivePosition,
				"Ground slab validation rejects non-finite positions");

			invalid = slab;
			invalid.m_Parameters.m_GroundSlab.m_HalfExtents.m_Z =
				0.0;
			context.Check(
				ValidatePrimitive(invalid).m_Error ==
					ValidationError::NonPositivePrimitiveExtent,
				"Primitive validation rejects a non-positive slab extent");

			invalid = sphere;
			invalid.m_Material = VoxelMaterial::Empty;
			context.Check(
				ValidatePrimitive(invalid).m_Error ==
					ValidationError::EmptyPrimitiveMaterial,
				"Primitive validation rejects the empty material");

			invalid = sphere;
			invalid.m_Material = static_cast<VoxelMaterial>(255);
			context.Check(
				ValidatePrimitive(invalid).m_Error ==
					ValidationError::InvalidVoxelMaterial,
				"Primitive validation rejects an unknown material");

			invalid = sphere;
			invalid.m_Shape = static_cast<PrimitiveShape>(255);
			context.Check(
				ValidatePrimitive(invalid).m_Error ==
					ValidationError::InvalidPrimitiveShape,
				"Primitive validation rejects an unknown shape");

			const std::array validSet{
				MakeSphere(10),
				MakeBox(11),
				MakeGroundSlab(12),
			};
			context.Check(
				ValidatePrimitiveSet(validSet).Succeeded() &&
					ValidatePrimitiveSet(
						std::span<const PrimitiveDesc>{}).Succeeded(),
				"Primitive set validation accepts unique and empty sets");

			const std::array duplicateSet{
				MakeSphere(17),
				MakeBox(17),
			};
			context.Check(
				ValidatePrimitiveSet(duplicateSet).m_Error ==
					ValidationError::DuplicatePrimitiveStableId,
				"Primitive set validation rejects duplicate stable IDs");
		}

		void RunSignedDistanceTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const PrimitiveDesc sphere = MakeSphere(1);
			context.Check(
				HasSignedDistance(sphere, { 1.0, 2.0, 3.0 }, -2.0) &&
					HasSignedDistance(
						sphere,
						{ 3.0, 2.0, 3.0 },
						0.0) &&
					HasSignedDistance(
						sphere,
						{ 4.0, 2.0, 3.0 },
						1.0),
				"Sphere SDF classifies center, surface, and outside");

			const PrimitiveDesc box = MakeBox(2);
			context.Check(
				HasSignedDistance(box, {}, -1.0) &&
					HasSignedDistance(box, { 1.0, 0.0, 0.0 }, 0.0) &&
					HasSignedDistance(box, { 1.0, 2.0, 0.0 }, 0.0) &&
					HasSignedDistance(box, { 1.0, 2.0, 3.0 }, 0.0) &&
					HasSignedDistance(box, { 2.0, 2.0, 3.0 }, 1.0),
				"Box SDF classifies center, face, edge, corner, and outside");

			const PrimitiveDesc slab = MakeGroundSlab(3);
			context.Check(
				HasSignedDistance(
					slab,
					{ 0.0, -1.5, 0.0 },
					0.0) &&
					HasSignedDistance(
						slab,
						{ 0.0, -2.5, 0.0 },
						0.0) &&
					HasSignedDistance(
						slab,
						{ 4.0, -2.0, 0.0 },
						0.0) &&
					HasSignedDistance(
						slab,
						{ 0.0, -2.0, 0.0 },
						-0.5),
				"Ground slab SDF represents a finite closed box");

			double unchanged = 37.0;
			context.Check(
				EvaluatePrimitiveSignedDistance(
					sphere,
					{
						std::numeric_limits<double>::infinity(),
						0.0,
						0.0,
					},
					unchanged).m_Error ==
					ValidationError::NonFinitePrimitivePosition &&
					unchanged == 37.0,
				"Signed-distance evaluation rejects a non-finite query");

			PrimitiveDesc extreme = sphere;
			extreme.m_Parameters.m_Sphere.m_Center.m_X =
				-std::numeric_limits<double>::max();
			context.Check(
				EvaluatePrimitiveSignedDistance(
					extreme,
					{
						std::numeric_limits<double>::max(),
						2.0,
						3.0,
					},
					unchanged).m_Error ==
					ValidationError::NonFiniteSignedDistance &&
					unchanged == 37.0,
				"Signed-distance evaluation rejects non-finite results");
		}
	}

	void RunNapaVoxelPrimitiveSelfTests(
		SelfTestContext& context) noexcept
	{
		RunPrimitiveLayoutTests(context);
		RunPrimitiveValidationTests(context);
		RunSignedDistanceTests(context);
	}
}
