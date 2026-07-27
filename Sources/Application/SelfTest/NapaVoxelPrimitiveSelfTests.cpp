#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/Field/DensityQuantization.h"
#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Hash/VoxelWorldHash.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

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

		[[nodiscard]] napa::voxel::VoxelWorldConfig
			MakePrimitiveWorldConfig(
				std::int32_t maximum = 8) noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = {},
					.m_MaxExclusive = {
						maximum,
						maximum,
						maximum,
					},
				},
			};
		}

		[[nodiscard]] napa::voxel::PrimitiveDesc
			MakeGeneratedSphere(
				std::uint64_t stableId,
				napa::voxel::Double3 center,
				double radius,
				napa::voxel::PrimitivePriority priority = {},
				napa::voxel::VoxelMaterial material =
					napa::voxel::VoxelMaterial::Stone) noexcept
		{
			napa::voxel::PrimitiveDesc sphere = MakeSphere(
				stableId,
				priority,
				material);
			sphere.m_Parameters.m_Sphere.m_Center = center;
			sphere.m_Parameters.m_Sphere.m_Radius = radius;
			return sphere;
		}

		[[nodiscard]] napa::voxel::PrimitiveDesc
			MakeGeneratedBox(
				std::uint64_t stableId,
				napa::voxel::Double3 center,
				napa::voxel::Double3 halfExtents) noexcept
		{
			napa::voxel::PrimitiveDesc box = MakeBox(stableId);
			box.m_Parameters.m_AxisAlignedBox.m_Center = center;
			box.m_Parameters.m_AxisAlignedBox.m_HalfExtents =
				halfExtents;
			return box;
		}

		[[nodiscard]] bool ReadCurrentSample(
			const napa::voxel::VoxelWorld& world,
			napa::voxel::SampleCoord coordinate,
			napa::voxel::VoxelSample& sample) noexcept
		{
			return world.ReadCurrentSample(
				coordinate,
				sample).Succeeded();
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

		void RunDensityQuantizationTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			std::int64_t rounded = 0;
			context.Check(
				RoundHalfAwayFromZero(0.5, rounded).Succeeded() &&
					rounded == 1 &&
					RoundHalfAwayFromZero(1.5, rounded).Succeeded() &&
					rounded == 2 &&
					RoundHalfAwayFromZero(-0.5, rounded).Succeeded() &&
					rounded == -1 &&
					RoundHalfAwayFromZero(-1.5, rounded).Succeeded() &&
					rounded == -2,
				"Density rounding resolves positive and negative halves away from zero");

			rounded = 37;
			context.Check(
				RoundHalfAwayFromZero(
					std::numeric_limits<double>::infinity(),
					rounded).m_Error ==
					ValidationError::NonFiniteQuantizationInput &&
					rounded == 37 &&
					RoundHalfAwayFromZero(
						std::numeric_limits<double>::max(),
						rounded).m_Error ==
						ValidationError::ArithmeticOverflow &&
					rounded == 37,
				"Density rounding rejects invalid inputs without changing output");

			std::uint8_t density = 0;
			context.Check(
				QuantizeSignedDistance(
					0.0,
					1.0f,
					2.0f,
					density).Succeeded() &&
					density == IsoValue &&
					QuantizeSignedDistance(
						-1.0 / 127.0,
						1.0f,
						2.0f,
						density).Succeeded() &&
					density == IsoValue + 1 &&
					QuantizeSignedDistance(
						1000.0,
						1.0f,
						2.0f,
						density).Succeeded() &&
					density == 0 &&
					QuantizeSignedDistance(
						-1000.0,
						1.0f,
						2.0f,
						density).Succeeded() &&
					density == 255,
				"SDF quantization fixes the iso point, half rounding, and clamping");

			density = 91;
			context.Check(
				QuantizeSignedDistance(
					std::numeric_limits<double>::quiet_NaN(),
					1.0f,
					2.0f,
					density).m_Error ==
					ValidationError::NonFiniteQuantizationInput &&
					density == 91,
				"SDF quantization leaves output unchanged on failure");

			DensityQuantizationContext quantizationContext;
			context.Check(
				QuantizeSignedDistance(
					0.0,
					quantizationContext,
					density).m_Error ==
					ValidationError::
						UnpreparedDensityQuantizationContext &&
					density == 91 &&
					PrepareDensityQuantizationContext(
						1.0f,
						2.0f,
						quantizationContext).Succeeded() &&
					QuantizeSignedDistance(
						0.0,
						quantizationContext,
						density).Succeeded() &&
					density == IsoValue,
				"Prepared quantization contexts validate world parameters once");
		}

		void RunPrimitiveUnionTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config =
				MakePrimitiveWorldConfig();
			const Double3 center{ 4.0, 4.0, 4.0 };
			const SampleCoord centerSample{ 4, 4, 4 };
			VoxelSample sample{};

			const std::array priorityTie{
				MakeGeneratedSphere(
					20,
					center,
					1.0,
					{ 0 },
					VoxelMaterial::Stone),
				MakeGeneratedSphere(
					10,
					center,
					1.0,
					{ 1 },
					VoxelMaterial::Soil),
			};
			std::unique_ptr<VoxelWorld> priorityWorld;
			PrimitiveWorldGenerationResult generation{};
			context.Check(
				GeneratePrimitiveVoxelWorld(
					config,
					priorityTie,
					priorityWorld,
					generation).Succeeded() &&
					priorityWorld &&
					ReadCurrentSample(
						*priorityWorld,
						centerSample,
						sample) &&
					sample.m_Material == VoxelMaterial::Soil,
				"Exact SDF ties prefer the higher primitive priority");

			const std::array stableIdTie{
				MakeGeneratedSphere(
					20,
					center,
					1.0,
					{},
					VoxelMaterial::Soil),
				MakeGeneratedSphere(
					10,
					center,
					1.0,
					{},
					VoxelMaterial::Stone),
			};
			std::unique_ptr<VoxelWorld> stableIdWorld;
			context.Check(
				GeneratePrimitiveVoxelWorld(
					config,
					stableIdTie,
					stableIdWorld,
					generation).Succeeded() &&
					stableIdWorld &&
					ReadCurrentSample(
						*stableIdWorld,
						centerSample,
						sample) &&
					sample.m_Material == VoxelMaterial::Stone,
				"Equal-priority SDF ties prefer the lower stable ID");

			const std::array distinctDistances{
				MakeGeneratedSphere(
					10,
					center,
					1.0,
					{ 100 },
					VoxelMaterial::Soil),
				MakeGeneratedSphere(
					20,
					center,
					1.0 + 1.0e-12,
					{ -100 },
					VoxelMaterial::Stone),
			};
			std::unique_ptr<VoxelWorld> distanceWorld;
			context.Check(
				GeneratePrimitiveVoxelWorld(
					config,
					distinctDistances,
					distanceWorld,
					generation).Succeeded() &&
					distanceWorld &&
					ReadCurrentSample(
						*distanceWorld,
						centerSample,
						sample) &&
					sample.m_Material == VoxelMaterial::Stone,
				"Primitive union does not apply an epsilon to distinct SDF values");
		}

		void RunPrimitiveWorldGenerationTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config =
				MakePrimitiveWorldConfig(16);
			std::vector<PrimitiveDesc> primitives{
				MakeGeneratedSphere(
					30,
					{ 8.0, 8.0, 8.0 },
					2.25),
				MakeGeneratedBox(
					10,
					{ 4.0, 4.0, 4.0 },
					{ 1.25, 1.5, 1.75 }),
				MakeGroundSlab(20),
			};
			primitives[2].m_Parameters.m_GroundSlab.m_Center =
				{ 11.0, 3.0, 8.0 };
			primitives[2].m_Parameters.m_GroundSlab.m_HalfExtents =
				{ 2.0, 0.5, 2.0 };

			std::unique_ptr<VoxelWorld> forwardWorld;
			PrimitiveWorldGenerationResult forwardResult{};
			const bool generatedForward =
				GeneratePrimitiveVoxelWorld(
					config,
					primitives,
					forwardWorld,
					forwardResult).Succeeded() &&
				forwardWorld;

			std::reverse(primitives.begin(), primitives.end());
			std::unique_ptr<VoxelWorld> reverseWorld;
			PrimitiveWorldGenerationResult reverseResult{};
			context.Check(
				generatedForward &&
					GeneratePrimitiveVoxelWorld(
						config,
						primitives,
						reverseWorld,
						reverseResult).Succeeded() &&
					reverseWorld &&
					forwardResult.m_InitialVoxelHash ==
						reverseResult.m_InitialVoxelHash,
				"Primitive input order does not affect the initial voxel hash");

			bool repeatedHashMatches = generatedForward;
			for (std::uint32_t iteration = 0;
				iteration < 10 && repeatedHashMatches;
				++iteration)
			{
				std::unique_ptr<VoxelWorld> repeatedWorld;
				PrimitiveWorldGenerationResult repeatedResult{};
				repeatedHashMatches =
					GeneratePrimitiveVoxelWorld(
						config,
						primitives,
						repeatedWorld,
						repeatedResult).Succeeded() &&
					repeatedWorld &&
					repeatedResult.m_InitialVoxelHash ==
						forwardResult.m_InitialVoxelHash;
			}
			context.Check(
				repeatedHashMatches,
				"Repeated primitive generation produces an identical voxel hash");

			const std::array boundaryPrimitive{
				MakeGeneratedSphere(
					1,
					{ 7.5, 8.0, 8.0 },
					1.25),
			};
			std::unique_ptr<VoxelWorld> boundaryWorld;
			PrimitiveWorldGenerationResult boundaryResult{};
			VoxelSample left{};
			VoxelSample right{};
			context.Check(
				GeneratePrimitiveVoxelWorld(
					config,
					boundaryPrimitive,
					boundaryWorld,
					boundaryResult).Succeeded() &&
					boundaryWorld &&
					ReadCurrentSample(
						*boundaryWorld,
						{ 7, 8, 8 },
						left) &&
					ReadCurrentSample(
						*boundaryWorld,
						{ 8, 8, 8 },
						right) &&
					left == right &&
					left.m_Density >= IsoValue &&
					boundaryWorld->FindChunk({ 0, 1, 1 }) != nullptr &&
					boundaryWorld->FindChunk({ 1, 1, 1 }) != nullptr,
				"Primitive generation preserves sample bytes across a chunk boundary");
		}

		void RunPrimitiveWorldInvariantTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config =
				MakePrimitiveWorldConfig();
			std::unique_ptr<VoxelWorld> unchangedWorld;
			context.Check(
				VoxelWorld::Create(
					config,
					unchangedWorld).Succeeded() &&
					unchangedWorld,
				"Primitive generation failure fixture creates a world");
			VoxelWorld* const unchangedAddress =
				unchangedWorld.get();
			PrimitiveWorldGenerationResult unchangedResult{
				.m_InitialVoxelHash = 0x123456789abcdef0ull,
			};
			const std::array unsafePrimitive{
				MakeGeneratedSphere(
					1,
					{ 1.0, 4.0, 4.0 },
					1.0),
			};
			context.Check(
				GeneratePrimitiveVoxelWorld(
					config,
					unsafePrimitive,
					unchangedWorld,
					unchangedResult).m_Error ==
					ValidationError::EmptySafetyMarginViolation &&
					unchangedWorld.get() == unchangedAddress &&
					unchangedResult.m_InitialVoxelHash ==
						0x123456789abcdef0ull,
				"Safety-margin failure does not publish a partial world");

			const std::array validPrimitive{
				MakeGeneratedSphere(
					42,
					{ 4.0, 4.0, 4.0 },
					1.5),
			};
			std::unique_ptr<VoxelWorld> generatedWorld;
			PrimitiveWorldGenerationResult generatedResult{};
			const bool generated =
				GeneratePrimitiveVoxelWorld(
					config,
					validPrimitive,
					generatedWorld,
					generatedResult).Succeeded() &&
				generatedWorld;
			const VoxelChunk* generatedChunk = generated
				? generatedWorld->FindChunk({ 0, 0, 0 })
				: nullptr;
			context.Check(
				generated &&
					generatedWorld->IsOriginalStateSealed() &&
					generatedWorld->GetWorldVoxelRevision() == 1 &&
					generatedChunk != nullptr &&
					generatedChunk->GetVoxelRevision() == 1,
				"Primitive generation publishes a sealed revision-one baseline");

			bool changed = true;
			context.Check(
				generated &&
					generatedWorld->WriteOriginalAndCurrentSample(
						{ 4, 4, 4 },
						DefaultVoxelSample,
						changed).m_Error ==
						ValidationError::OriginalStateSealed &&
					changed,
				"Sealed primitive worlds reject later Original writes");

			std::uint64_t restoredHash = 0;
			RestoreResult restore{};
			context.Check(
				generated &&
					generatedWorld->WriteCurrentSample(
						{ 4, 4, 4 },
						DefaultVoxelSample,
						changed).Succeeded() &&
					changed &&
					generatedWorld->RestoreAll(restore).Succeeded() &&
					restore.Changed() &&
					ComputeLogicalVoxelWorldHash(
						*generatedWorld,
						restoredHash).Succeeded() &&
					restoredHash ==
						generatedResult.m_InitialVoxelHash,
				"Restore returns edited Current data to the generated baseline");

			context.Check(
				generated &&
					generatedResult.m_InitialVoxelHash ==
						0x1c13954365d53eafull,
				"A single-chunk primitive world matches its golden hash");

			const VoxelWorldConfig boundaryConfig =
				MakePrimitiveWorldConfig(16);
			const std::array boundaryPrimitive{
				MakeGeneratedSphere(
					42,
					{ 7.5, 8.0, 8.0 },
					1.25),
			};
			std::unique_ptr<VoxelWorld> boundaryWorld;
			PrimitiveWorldGenerationResult boundaryResult{};
			context.Check(
				GeneratePrimitiveVoxelWorld(
					boundaryConfig,
					boundaryPrimitive,
					boundaryWorld,
					boundaryResult).Succeeded() &&
					boundaryWorld &&
					boundaryResult.m_InitialVoxelHash ==
						0xd1d37ab06b383ad6ull,
				"A chunk-boundary primitive world matches its golden hash");
		}
	}

	void RunNapaVoxelPrimitiveSelfTests(
		SelfTestContext& context) noexcept
	{
		RunPrimitiveLayoutTests(context);
		RunPrimitiveValidationTests(context);
		RunSignedDistanceTests(context);
		RunDensityQuantizationTests(context);
		RunPrimitiveUnionTests(context);
		RunPrimitiveWorldGenerationTests(context);
		RunPrimitiveWorldInvariantTests(context);
	}
}
