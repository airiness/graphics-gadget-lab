#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/Edit/SphereEdit.h"
#include "NapaVoxelCore/World/VoxelSample.h"

#include <array>
#include <limits>
#include <type_traits>
#include <vector>

namespace gglab
{
	namespace
	{
		struct EligibilityRecord
		{
			napa::voxel::SampleCoord m_Sample{};
			napa::voxel::SphereEditSampleEvaluation m_Evaluation{};

			[[nodiscard]] friend bool operator==(
				const EligibilityRecord&, const EligibilityRecord&) noexcept = default;
		};

		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeEditConfig(
			float voxelSize = 1.0f, float surfaceBand = 2.0f,
			napa::voxel::CellAabb bounds = {
				.m_Min = { -8, -8, -8 },
				.m_MaxExclusive = { 8, 8, 8 },
			}) noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = voxelSize,
				.m_SurfaceBandVoxels = surfaceBand,
				.m_LogicalCellBounds = bounds,
			};
		}

		[[nodiscard]] napa::voxel::SphereEditRequest MakeEditRequest(
			napa::voxel::Double3 center = {}, double radius = 1.0, double strength = 1.0,
			std::uint8_t damagePerHit = 0) noexcept
		{
			return {
				.m_Brush = {
					.m_CenterWorld = center,
					.m_Radius = radius,
					.m_Strength = strength,
				},
				.m_MaterialRules = {
					.m_DamagePerHit = damagePerHit,
					.m_StoneBreakThreshold = 255,
				},
			};
		}

		[[nodiscard]] bool ScanEligibility(const napa::voxel::SphereEditContext& context,
			const napa::voxel::SampleAabb& bounds, std::vector<EligibilityRecord>& records)
		{
			std::vector<EligibilityRecord> prepared;
			for (std::int32_t z = bounds.m_Min.m_Z; z < bounds.m_MaxExclusive.m_Z; ++z)
			{
				for (std::int32_t y = bounds.m_Min.m_Y; y < bounds.m_MaxExclusive.m_Y; ++y)
				{
					for (std::int32_t x = bounds.m_Min.m_X; x < bounds.m_MaxExclusive.m_X; ++x)
					{
						const napa::voxel::SampleCoord sample{ x, y, z };
						napa::voxel::SphereEditSampleEvaluation evaluation{};
						if (napa::voxel::EvaluateSphereEditSample(
							context, sample, evaluation).Failed())
						{
							return false;
						}
						if (evaluation.m_DensityPathEligible || evaluation.m_DamagePathEligible)
						{
							prepared.push_back({ sample, evaluation });
						}
					}
				}
			}
			records = std::move(prepared);
			return true;
		}

		void RunEditTypeAndValidationTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			context.Check(std::is_standard_layout_v<SphereEdit> &&
				std::is_trivially_copyable_v<SphereEdit> &&
				std::is_standard_layout_v<VoxelEditMaterialRules> &&
				std::is_trivially_copyable_v<VoxelEditMaterialRules> &&
				std::is_standard_layout_v<SphereEditRequest> &&
				std::is_trivially_copyable_v<SphereEditRequest>,
				"Sphere edit requests use portable value layouts");

			const VoxelEditMaterialRules defaultRules{};
			context.Check(defaultRules.m_DamagePerHit == 128 &&
				defaultRules.m_StoneBreakThreshold == 255,
				"Sphere edit material rules expose the canonical defaults");

			const SphereEditRequest valid = MakeEditRequest({ -0.25, 0.5, 1.25 }, 2.0, 0.5, 0);
			context.Check(ValidateEdit(valid).Succeeded(),
				"Sphere edit validation accepts a finite Double3 and double request");

			SphereEditRequest invalid = valid;
			invalid.m_Brush.m_CenterWorld.m_X = std::numeric_limits<double>::infinity();
			context.Check(ValidateEdit(invalid).m_Error == ValidationError::NonFiniteEditPosition,
				"Sphere edit validation rejects a non-finite center");

			invalid = valid;
			invalid.m_Brush.m_Radius = std::numeric_limits<double>::quiet_NaN();
			context.Check(ValidateEdit(invalid).m_Error == ValidationError::NonFiniteEditRadius,
				"Sphere edit validation rejects a non-finite radius");

			invalid = valid;
			invalid.m_Brush.m_Radius = 0.0;
			context.Check(ValidateEdit(invalid).m_Error == ValidationError::NonPositiveEditRadius,
				"Sphere edit validation rejects a non-positive radius");

			invalid = valid;
			invalid.m_Brush.m_Strength = -std::numeric_limits<double>::infinity();
			context.Check(ValidateEdit(invalid).m_Error == ValidationError::NonFiniteEditStrength,
				"Sphere edit validation rejects a non-finite strength");

			invalid = valid;
			invalid.m_MaterialRules.m_StoneBreakThreshold = 0;
			context.Check(ValidateEdit(invalid).m_Error ==
				ValidationError::InvalidVoxelEditMaterialRules,
				"Sphere edit validation rejects a zero Stone break threshold");

			SphereEditContext negativeStrength{};
			SphereEditContext midpointStrength{};
			SphereEditContext excessiveStrength{};
			const bool strengthsPrepared = PrepareSphereEditContext(MakeEditConfig(),
				MakeEditRequest({}, 1.0, -2.0, 0), negativeStrength).Succeeded() &&
				PrepareSphereEditContext(MakeEditConfig(),
					MakeEditRequest({}, 1.0, 0.5, 0), midpointStrength).Succeeded() &&
				PrepareSphereEditContext(MakeEditConfig(),
					MakeEditRequest({}, 1.0, 2.0, 0), excessiveStrength).Succeeded();
			context.Check(strengthsPrepared && negativeStrength.GetSanitizedStrength() == 0.0 &&
				!negativeStrength.HasDensityPath() &&
				midpointStrength.GetSanitizedStrength() == 0.5 &&
				excessiveStrength.GetSanitizedStrength() == 1.0,
				"Finite sphere edit strengths clamp deterministically to zero and one");
		}

		void RunEditBoundsTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			const VoxelWorldConfig config = MakeEditConfig();
			SphereEditContext densityContext{};
			const bool densityPrepared = PrepareSphereEditContext(config,
				MakeEditRequest({ -0.25, 0.5, 1.25 }, 2.0, 1.0, 0), densityContext).Succeeded();
			const SampleAabb expectedDensityBounds{
				.m_Min = { -4, -3, -2 },
				.m_MaxExclusive = { 4, 5, 6 },
			};
			context.Check(densityPrepared && densityContext.HasDensityPath() &&
				!densityContext.HasDamagePath() &&
				densityContext.GetScanBounds() == expectedDensityBounds,
				"Density edits use ceil minima and floor-plus-one maxima");

			SphereEditContext damageContext{};
			const bool damagePrepared = PrepareSphereEditContext(config,
				MakeEditRequest({ -0.25, 0.5, 1.25 }, 2.0, 0.0, 1), damageContext).Succeeded();
			const SampleAabb expectedDamageBounds{
				.m_Min = { -2, -1, 0 },
				.m_MaxExclusive = { 2, 3, 4 },
			};
			context.Check(damagePrepared && !damageContext.HasDensityPath() &&
				damageContext.HasDamagePath() &&
				damageContext.GetScanBounds() == expectedDamageBounds,
				"Damage edits use their narrower complete conservative bounds");

			SphereEditContext disabledContext{};
			const bool disabledPrepared = PrepareSphereEditContext(config,
				MakeEditRequest({}, 1.0, 0.0, 0), disabledContext).Succeeded();
			context.Check(disabledPrepared && !disabledContext.HasDensityPath() &&
				!disabledContext.HasDamagePath() && disabledContext.GetScanBounds().IsEmpty(),
				"A request with both edit paths disabled prepares an empty scan");

			const VoxelWorldConfig clippedConfig = MakeEditConfig(1.0f, 2.0f, {
				.m_Min = { -4, -4, -4 },
				.m_MaxExclusive = { 4, 4, 4 },
				});
			SphereEditContext clippedContext{};
			const bool clippedPrepared = PrepareSphereEditContext(clippedConfig,
				MakeEditRequest({ -4.0, -4.0, -4.0 }, 1.0, 1.0, 0), clippedContext).Succeeded();
			context.Check(clippedPrepared &&
				clippedContext.GetLogicalSampleBounds().Contains(clippedContext.GetScanBounds().m_Min) &&
				clippedContext.GetLogicalSampleBounds().Contains({
					clippedContext.GetScanBounds().m_MaxExclusive.m_X - 1,
					clippedContext.GetScanBounds().m_MaxExclusive.m_Y - 1,
					clippedContext.GetScanBounds().m_MaxExclusive.m_Z - 1,
					}),
					"Sphere edit scan bounds clip to the logical Sample Domain");

			const SampleAabb preservedBounds = densityContext.GetScanBounds();
			const double preservedStrength = densityContext.GetSanitizedStrength();
			const SphereEditRequest overflow = MakeEditRequest({
				static_cast<double>(std::numeric_limits<std::int32_t>::max()), 0.0, 0.0,
				}, 2.0, 1.0, 0);
			const ValidationResult overflowResult =
				PrepareSphereEditContext(config, overflow, densityContext);
			context.Check(overflowResult.m_Error == ValidationError::ArithmeticOverflow &&
				densityContext.GetScanBounds() == preservedBounds &&
				densityContext.GetSanitizedStrength() == preservedStrength,
				"Coordinate-limit overflow leaves a prepared edit context unchanged");

			const SphereEditRequest radiusAdditionOverflow = MakeEditRequest(
				{}, std::numeric_limits<double>::max(), 1.0, 0);
			const ValidationResult radiusAdditionResult = PrepareSphereEditContext(
				config, radiusAdditionOverflow, densityContext);
			context.Check(radiusAdditionResult.m_Error == ValidationError::ArithmeticOverflow &&
				densityContext.GetScanBounds() == preservedBounds &&
				densityContext.GetSanitizedStrength() == preservedStrength,
				"A finite radius whose support addition overflows leaves the context unchanged");
		}

		void RunEditEligibilityTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			SphereEditContext basicContext{};
			const bool basicPrepared = PrepareSphereEditContext(MakeEditConfig(),
				MakeEditRequest({}, 1.0, 1.0, 0), basicContext).Succeeded();
			SphereEditSampleEvaluation center{};
			SphereEditSampleEvaluation surface{};
			SphereEditSampleEvaluation outside{};
			const bool basicEvaluated = basicPrepared &&
				EvaluateSphereEditSample(basicContext, {}, center).Succeeded() &&
				EvaluateSphereEditSample(basicContext, { 1, 0, 0 }, surface).Succeeded() &&
				EvaluateSphereEditSample(basicContext, { 4, 0, 0 }, outside).Succeeded();
			context.Check(basicEvaluated && center.m_BrushDensity == 192 &&
				surface.m_BrushDensity == IsoValue && outside.m_BrushDensity == 0 &&
				center.m_DensityPathEligible && surface.m_DensityPathEligible &&
				!outside.m_DensityPathEligible,
				"Sphere edit Brush quantization classifies center, surface, and outside");

			const VoxelWorldConfig supportConfig = MakeEditConfig(1.0f, 127.0f, {
				.m_Min = { -128, -128, -128 },
				.m_MaxExclusive = { 128, 128, 128 },
				});
			SphereEditContext densityContext{};
			const bool densityPrepared = PrepareSphereEditContext(supportConfig,
				MakeEditRequest({}, 0.5, 1.0, 0), densityContext).Succeeded();
			SphereEditSampleEvaluation positiveDensity{};
			SphereEditSampleEvaluation negativeDensity{};
			SphereEditSampleEvaluation outsideDensity{};
			const bool densityEvaluated = densityPrepared &&
				EvaluateSphereEditSample(densityContext, { 127, 0, 0 }, positiveDensity).Succeeded() &&
				EvaluateSphereEditSample(densityContext, { -127, 0, 0 }, negativeDensity).Succeeded() &&
				EvaluateSphereEditSample(densityContext, { 128, 0, 0 }, outsideDensity).Succeeded();
			context.Check(densityEvaluated && positiveDensity.m_BrushDensity == 2 &&
				negativeDensity.m_BrushDensity == 2 &&
				positiveDensity.m_DensityPathEligible && negativeDensity.m_DensityPathEligible &&
				outsideDensity.m_BrushDensity == 1 &&
				!outsideDensity.m_DensityPathEligible &&
				densityContext.GetScanBounds().Contains({ 127, 0, 0 }) &&
				!densityContext.GetScanBounds().Contains({ 128, 0, 0 }),
				"Positive and negative density support ties are included exactly once");

			SphereEditContext damageContext{};
			const bool damagePrepared = PrepareSphereEditContext(supportConfig,
				MakeEditRequest({}, 0.5, 0.0, 1), damageContext).Succeeded();
			SphereEditSampleEvaluation damageBoundary{};
			SphereEditSampleEvaluation damageOutside{};
			const bool damageEvaluated = damagePrepared &&
				EvaluateSphereEditSample(damageContext, { 1, 0, 0 }, damageBoundary).Succeeded() &&
				EvaluateSphereEditSample(damageContext, { 2, 0, 0 }, damageOutside).Succeeded();
			context.Check(damageEvaluated && damageBoundary.m_BrushDensity == IsoValue &&
				damageBoundary.m_DamagePathEligible &&
				damageOutside.m_BrushDensity == IsoValue - 1 &&
				!damageOutside.m_DamagePathEligible,
				"Damage eligibility includes its outer half-away-from-zero support tie");

			SphereEditContext cornerContext{};
			const bool cornerPrepared = PrepareSphereEditContext(MakeEditConfig(),
				MakeEditRequest({}, 1.0, 1.0, 0), cornerContext).Succeeded();
			SphereEditSampleEvaluation corner{};
			const SampleCoord cornerSample{ 2, 2, 2 };
			const bool cornerEvaluated = cornerPrepared &&
				cornerContext.GetScanBounds().Contains(cornerSample) &&
				EvaluateSphereEditSample(cornerContext, cornerSample, corner).Succeeded();
			context.Check(cornerEvaluated && !corner.m_DensityPathEligible &&
				!corner.m_DamagePathEligible,
				"A conservative Brush Bounds corner is not treated as a Sphere hit");

			SphereEditSampleEvaluation preserved{
				.m_BrushDensity = 77,
				.m_DensityPathEligible = true,
				.m_DamagePathEligible = true,
			};
			const SphereEditSampleEvaluation expectedPreserved = preserved;
			SphereEditContext unprepared{};
			const ValidationResult unpreparedResult =
				EvaluateSphereEditSample(unprepared, {}, preserved);
			const ValidationResult outsideResult =
				EvaluateSphereEditSample(cornerContext, { 100, 0, 0 }, preserved);
			context.Check(unpreparedResult.m_Error == ValidationError::UnpreparedSphereEditContext &&
				outsideResult.m_Error == ValidationError::SampleOutsideLogicalBounds &&
				preserved == expectedPreserved,
				"Rejected sphere edit evaluation leaves its output unchanged");
		}

		void RunEditBoundsOracleTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			struct OracleCase
			{
				VoxelWorldConfig m_Config{};
				SphereEditRequest m_Request{};
			};
			const std::array cases{
				OracleCase{ MakeEditConfig(),
					MakeEditRequest({ -0.25, 0.5, 1.25 }, 2.0, 1.0, 0) },
				OracleCase{ MakeEditConfig(),
					MakeEditRequest({}, 1.0, 0.0, 128) },
				OracleCase{ MakeEditConfig(0.5f, 4.0f),
					MakeEditRequest({ -2.25, 3.5, -0.75 }, 0.75, 0.5, 64) },
				OracleCase{ MakeEditConfig(1.0f, 1.0f, {
						.m_Min = { -4, -4, -4 },
						.m_MaxExclusive = { 4, 4, 4 },
					}), MakeEditRequest({ -3.75, 0.25, 0.5 }, 1.0, 1.0, 0) },
				OracleCase{ MakeEditConfig(), MakeEditRequest({}, 1.0, 0.0, 0) },
			};

			bool allCasesMatched = true;
			for (const OracleCase& oracleCase : cases)
			{
				SphereEditContext editContext{};
				if (PrepareSphereEditContext(
					oracleCase.m_Config, oracleCase.m_Request, editContext).Failed())
				{
					allCasesMatched = false;
					break;
				}

				std::vector<EligibilityRecord> fullDomainRecords;
				std::vector<EligibilityRecord> boundedRecords;
				if (!ScanEligibility(editContext, editContext.GetLogicalSampleBounds(),
					fullDomainRecords) ||
					!ScanEligibility(editContext, editContext.GetScanBounds(), boundedRecords) ||
					fullDomainRecords != boundedRecords ||
					!std::ranges::all_of(fullDomainRecords,
						[&editContext](const EligibilityRecord& record) noexcept
						{ return editContext.GetScanBounds().Contains(record.m_Sample); }))
				{
					allCasesMatched = false;
					break;
				}
			}
			context.Check(allCasesMatched,
				"Full-domain Brush eligibility Oracle is exactly contained by production bounds");
		}
	}

	void RunNapaVoxelEditSelfTests(SelfTestContext& context) noexcept
	{
		RunEditTypeAndValidationTests(context);
		RunEditBoundsTests(context);
		RunEditEligibilityTests(context);
		RunEditBoundsOracleTests(context);
	}
}
