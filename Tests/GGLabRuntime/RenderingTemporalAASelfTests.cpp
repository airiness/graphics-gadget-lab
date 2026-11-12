#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/CameraController.h"
#include "GGLabRuntime/Graphics/CameraRenderViewQueryBase.h"
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelinePresets.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessColorState.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineSceneExtensionBase.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/ScreenSpace/ScreenSpaceTypes.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Pipeline/TemporalAACapability.h"
#include "Graphics/Pipeline/TemporalMotion.h"
#include "Graphics/RenderPass/RenderPassDepthPrepass.h"
#include "Graphics/RenderPass/TemporalGeometryGraphResources.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "Graphics/Renderer.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>

namespace gglab::rendering_test
{
	class IntegratedTemporalSceneExtension final : public RenderPipelineSceneExtensionBase
	{
	public:
		SceneExtensionTemporalParticipation GetTemporalParticipation() const noexcept override
		{
			return SceneExtensionTemporalParticipation::TemporalIntegrated;
		}

		void AddOpaqueScenePasses(RenderGraph&, const RenderFrameContext&, const RenderServices&) noexcept override
		{
		}
	};

	// CPU reference helpers for the temporal shader contracts below.
	struct TemporalMotionReadbackSample
	{
		Vector2 m_MotionUV = Vector2::Zero;
		Vector2 m_MotionPixels = Vector2::Zero;
		float m_MagnitudePixels = 0.0f;
		bool m_Valid = false;
	};

	[[nodiscard]] inline Vector2 TemporalClipPositionToUV(const Vector4& clipPosition) noexcept
	{
		if (!std::isfinite(clipPosition.m_X) || !std::isfinite(clipPosition.m_Y) ||
			!std::isfinite(clipPosition.m_W) || std::abs(clipPosition.m_W) <= 1.0e-6f)
		{
			return Vector2::Zero;
		}
		const float inverseW = 1.0f / clipPosition.m_W;
		return Vector2(clipPosition.m_X * inverseW * 0.5f + 0.5f, -clipPosition.m_Y * inverseW * 0.5f + 0.5f);
	}

	[[nodiscard]] inline Vector2 ComputeTemporalMotionUV(const Vector4& currentClip, const Vector4& previousClip) noexcept
	{
		return TemporalClipPositionToUV(currentClip) - TemporalClipPositionToUV(previousClip);
	}

	[[nodiscard]] inline TemporalMotionReadbackSample ResolveTemporalMotionReadbackSample(
		const Vector2& motionUV, uint32_t width, uint32_t height) noexcept
	{
		if (!std::isfinite(motionUV.m_X) || !std::isfinite(motionUV.m_Y) || width == 0 || height == 0)
		{
			return {};
		}
		const Vector2 motionPixels(motionUV.m_X * static_cast<float>(width), motionUV.m_Y * static_cast<float>(height));
		return {
			.m_MotionUV = motionUV,
			.m_MotionPixels = motionPixels,
			.m_MagnitudePixels = std::sqrt(motionPixels.m_X * motionPixels.m_X + motionPixels.m_Y * motionPixels.m_Y),
			.m_Valid = true,
		};
	}

	[[nodiscard]] inline bool IsTemporalHistoryAccumulationValid(float accumulation) noexcept
	{
		return std::isfinite(accumulation) &&
			accumulation >= TemporalHistoryInitialAccumulation &&
			accumulation <= TemporalHistoryMaxAccumulation;
	}

	[[nodiscard]] inline float ResolveTemporalHistoryNextAge(bool historyAccepted, float previousHistoryAge) noexcept
	{
		return historyAccepted && IsTemporalHistoryAccumulationValid(previousHistoryAge) ?
			std::min(previousHistoryAge + 1.0f, TemporalHistoryMaxAccumulation) :
			TemporalHistoryInitialAccumulation;
	}

	[[nodiscard]] inline float ResolveTemporalHistoryNextSamples(bool historyAccepted,
		float previousSamples, float historyConfidence, float maxSamples,
		float currentSampleWeight = 1.0f) noexcept
	{
		if (!historyAccepted || !IsTemporalHistoryAccumulationValid(previousSamples) || !std::isfinite(historyConfidence))
		{
			return TemporalHistoryInitialAccumulation;
		}
		return std::clamp(std::clamp(historyConfidence, 0.0f, 1.0f) *
			std::min(previousSamples, maxSamples) + currentSampleWeight,
			TemporalHistoryInitialAccumulation, std::max(maxSamples, TemporalHistoryInitialAccumulation));
	}

	inline constexpr float TemporalUpscaledStaticMotionPixels = 0.5f;

	inline constexpr float TemporalUpscaledMinSampleWeight = 1.0e-3f;

	[[nodiscard]] inline float ResolveTemporalUpscaledStaticFraction(float motionMagnitudePixels) noexcept
	{
		return std::isfinite(motionMagnitudePixels) ?
			1.0f - std::clamp(motionMagnitudePixels / TemporalUpscaledStaticMotionPixels, 0.0f, 1.0f) :
			0.0f;
	}

	[[nodiscard]] inline float ResolveTemporalUpscaledStaticClampExpansion(float motionMagnitudePixels, float consistency) noexcept
	{
		return ResolveTemporalUpscaledStaticFraction(motionMagnitudePixels) *
			std::clamp((0.95f - consistency) / 0.15f, 0.0f, 1.0f);
	}

	[[nodiscard]] inline float ResolveTemporalNearestSampleWeight(Vector2 offsetPixels, Vector2 displayPerRender, float kernelScale) noexcept
	{
		const float x = (std::round(offsetPixels.m_X) - offsetPixels.m_X) * displayPerRender.m_X;
		const float y = (std::round(offsetPixels.m_Y) - offsetPixels.m_Y) * displayPerRender.m_Y;
		return std::exp(-kernelScale * (x * x + y * y));
	}

	[[nodiscard]] inline float ResolveTemporalUpscaledSampleWeight(float nearestSampleWeight) noexcept
	{
		return std::max(std::clamp(nearestSampleWeight, 0.0f, 1.0f), TemporalUpscaledMinSampleWeight);
	}

	[[nodiscard]] inline float ComputeTemporalWeightedHistoryWeight(float previousSamples,
		float historyConfidence, float maxSamples, float currentSampleWeight) noexcept
	{
		if (!IsTemporalHistoryAccumulationValid(previousSamples) || !std::isfinite(historyConfidence))
		{
			return 0.0f;
		}
		const float samples = std::min(previousSamples, maxSamples);
		return std::clamp(historyConfidence, 0.0f, 1.0f) * samples /
			(samples + std::max(currentSampleWeight, TemporalUpscaledMinSampleWeight));
	}

	[[nodiscard]] inline float ResolveTemporalDisagreementConsistency(float signedDifference, float absoluteDifference) noexcept
	{
		return std::clamp(std::abs(signedDifference) / std::max(absoluteDifference, 1.0e-4f), 0.0f, 1.0f);
	}

	[[nodiscard]] inline float ResolveTemporalHistoryRelaxation(float historyRelaxation,
		float accumulation, float maxSamples, float consistency) noexcept
	{
		const float accumulated = std::clamp(
			(accumulation - TemporalHistoryInitialAccumulation) /
			std::max(maxSamples - TemporalHistoryInitialAccumulation, 1.0f), 0.0f, 1.0f);
		return std::max(historyRelaxation, 0.0f) * accumulated *
			std::clamp((0.5f - consistency) / 0.3f, 0.0f, 1.0f);
	}

	[[nodiscard]] inline float ResolveTemporalAAHistorySamplesPreview(float nextSamples, float maxSamples) noexcept
	{
		if (!IsTemporalHistoryAccumulationValid(nextSamples))
		{
			return 0.0f;
		}
		return std::clamp((nextSamples - TemporalHistoryInitialAccumulation) /
			std::max(maxSamples - TemporalHistoryInitialAccumulation, 1.0e-6f), 0.0f, 1.0f);
	}

	struct TemporalAAOutputAlphaContract final
	{
		float m_ResolvedAlpha = 1.0f;
		float m_HistoryAlpha = TemporalHistoryInitialAccumulation;
	};

	[[nodiscard]] inline TemporalAAOutputAlphaContract ResolveTemporalAAOutputAlphas(float nextHistoryAge) noexcept
	{
		return {
			.m_ResolvedAlpha = 1.0f,
			.m_HistoryAlpha = IsTemporalHistoryAccumulationValid(nextHistoryAge) ?
				nextHistoryAge :
				TemporalHistoryInitialAccumulation,
		};
	}

	[[nodiscard]] inline Vector2 ResolveTemporalHistoryMotionUV(
		const Vector2& rasterMotionUV, const Vector2& currentJitterUV,
		const Vector2& previousJitterUV) noexcept
	{
		return rasterMotionUV - (currentJitterUV - previousJitterUV);
	}

	[[nodiscard]] inline bool IsTemporalAAUVInBounds(const Vector2& uv) noexcept
	{
		return std::isfinite(uv.m_X) && std::isfinite(uv.m_Y) &&
			uv.m_X >= 0.0f && uv.m_X <= 1.0f &&
			uv.m_Y >= 0.0f && uv.m_Y <= 1.0f;
	}

	[[nodiscard]] inline bool AreTemporalAAReprojectionUVsValid(const Vector2& previousHistoryUV, const Vector2& previousRasterUV) noexcept
	{
		return IsTemporalAAUVInBounds(previousHistoryUV) &&
			IsTemporalAAUVInBounds(previousRasterUV);
	}

	[[nodiscard]] inline float ResolveTemporalAAHistoryAgePreview(float nextHistoryAge, float maxHistoryFeedback) noexcept
	{
		if (!IsTemporalHistoryAccumulationValid(nextHistoryAge))
		{
			return 0.0f;
		}

		const float feedbackSaturationAge = ResolveTemporalAAFeedbackSaturationAge(maxHistoryFeedback);
		// Feedback uses PreviousAge while this preview displays stored NextAge.
		// Keep the saturation-age denominator intact; there is intentionally no -1.
		return std::clamp((nextHistoryAge - TemporalHistoryInitialAccumulation) /
			std::max(feedbackSaturationAge, TemporalHistoryInitialAccumulation), 0.0f, 1.0f);
	}

	[[nodiscard]] inline float ResolveTemporalAAHistoryWeight(float previousHistoryAge,
		float motionMagnitudePixels, float currentLuminance, float historyLuminance,
		const TemporalAASettings& settings) noexcept
	{
		if (!IsTemporalHistoryAccumulationValid(previousHistoryAge) ||
			!std::isfinite(motionMagnitudePixels) || motionMagnitudePixels < 0.0f ||
			!std::isfinite(currentLuminance) || !std::isfinite(historyLuminance))
		{
			return 0.0f;
		}

		const TemporalAASettings resolved = ResolveTemporalAASettings(settings);
		const float ageWeight = previousHistoryAge / (previousHistoryAge + 1.0f);
		const float baseHistoryWeight = std::min(ageWeight, resolved.m_MaxHistoryFeedback);
		const float velocityConfidence = 1.0f - std::clamp(motionMagnitudePixels * resolved.m_VelocityWeightScale, 0.0f, 1.0f);
		const float luminanceDenominator = std::max({ std::abs(currentLuminance), std::abs(historyLuminance), 1.0e-4f });
		const float relativeLuminanceDifference = std::abs(currentLuminance - historyLuminance) / luminanceDenominator;
		const float luminanceConfidence = 1.0f - std::clamp(relativeLuminanceDifference * resolved.m_LuminanceWeightScale, 0.0f, 1.0f);
		return baseHistoryWeight * velocityConfidence * luminanceConfidence;
	}

	[[nodiscard]] inline bool IsTemporalSkyHistoryCompatible(float previousRawDepth, DepthConvention previousDepthConvention) noexcept
	{
		return std::isfinite(previousRawDepth) &&
			screen_space::IsDepthBackground(previousRawDepth, previousDepthConvention);
	}

	[[nodiscard]] inline bool IsTemporalHistoryDepthCompatible(float expectedPreviousViewZ,
		float storedPreviousViewZ,
		float absoluteThreshold = TemporalAADepthAbsoluteThreshold,
		float relativeThreshold = TemporalAADepthRelativeThreshold) noexcept
	{
		if (!std::isfinite(expectedPreviousViewZ) || !std::isfinite(storedPreviousViewZ) ||
			expectedPreviousViewZ <= 0.0f || storedPreviousViewZ <= 0.0f ||
			!std::isfinite(absoluteThreshold) || !std::isfinite(relativeThreshold) ||
			absoluteThreshold < 0.0f || relativeThreshold < 0.0f)
		{
			return false;
		}

		const float tolerance = std::max(absoluteThreshold, relativeThreshold * expectedPreviousViewZ);
		return std::abs(expectedPreviousViewZ - storedPreviousViewZ) <= tolerance;
	}

	struct TemporalAAGeometryDepthSample
	{
		float m_StoredPreviousViewZ = 0.0f;
		bool m_IsGeometry = false;
	};

	[[nodiscard]] inline bool HasCompatibleTemporalGeometryDepthSample(
		float expectedPreviousViewZ,
		const std::array<TemporalAAGeometryDepthSample, 9>& previousDepthNeighborhood,
		float absoluteThreshold = TemporalAADepthAbsoluteThreshold,
		float relativeThreshold = TemporalAADepthRelativeThreshold) noexcept
	{
		for (const TemporalAAGeometryDepthSample& sample : previousDepthNeighborhood)
		{
			if (sample.m_IsGeometry && IsTemporalHistoryDepthCompatible(
				expectedPreviousViewZ, sample.m_StoredPreviousViewZ,
				absoluteThreshold, relativeThreshold))
			{
				return true;
			}
		}

		return false;
	}

	void RunTemporalCompatibilityAndHistoryContractTests(SelfTestContext& context) noexcept
	{
		static_assert(sizeof(ViewGPU) == 496);
		static_assert(offsetof(ViewGPU, PreviousViewMat) == 256);
		static_assert(offsetof(ViewGPU, PreviousDepthReconstructionParams) == 400);
		static_assert(offsetof(ViewGPU, CurrentJitterUV) == 432);
		static_assert(offsetof(ViewGPU, PreviousDepthConvention) == 464);
		static_assert(offsetof(ViewGPU, ScenePreExposure) == 468);
		static_assert(offsetof(ViewGPU, PreviousScenePreExposure) == 472);

		const Vector2 staticMotion = ComputeTemporalMotionUV(Vector4(0.0f, 0.0f, 0.5f, 1.0f), Vector4(0.0f, 0.0f, 0.5f, 1.0f));
		const Vector2 cameraMotion = ComputeTemporalMotionUV(Vector4(-0.2f, 0.0f, 0.5f, 1.0f), Vector4(0.0f, 0.0f, 0.5f, 1.0f));
		const Vector2 objectMotion = ComputeTemporalMotionUV(Vector4(0.4f, 0.2f, 0.5f, 1.0f), Vector4(0.0f, 0.0f, 0.5f, 1.0f));
		const TemporalMotionReadbackSample objectMotionReadback = ResolveTemporalMotionReadbackSample(objectMotion, 100, 50);
		context.Check(NearlyEqual(staticMotion, Vector2::Zero) &&
			NearlyEqual(cameraMotion, Vector2(-0.1f, 0.0f)) &&
			NearlyEqual(objectMotion, Vector2(0.2f, -0.1f)) &&
			objectMotionReadback.m_Valid &&
			NearlyEqual(objectMotionReadback.m_MotionPixels, Vector2(20.0f, -5.0f)) &&
			NearlyEqual(objectMotionReadback.m_MagnitudePixels, std::sqrt(425.0f)) &&
			!ResolveTemporalMotionReadbackSample(objectMotion, 0, 50).m_Valid,
			"Temporal motion uses current-minus-previous top-left UV delta and deterministic pixel magnitude");

		const Vector2 currentJitterUV = temporal::JitterPixelsToUV(Vector2(0.25f, -0.25f), 100, 50);
		const Vector2 previousJitterUV = temporal::JitterPixelsToUV(Vector2(-0.25f, 0.25f), 100, 50);
		const Vector4 staticUnjitteredClip(0.2f, -0.1f, 0.5f, 1.0f);
		const Vector4 currentRasterClip = temporal::ApplyJitterToClipPosition(
			staticUnjitteredClip,
			temporal::JitterPixelsToNDC(Vector2(0.25f, -0.25f), 100, 50));
		const Vector4 previousRasterClip = temporal::ApplyJitterToClipPosition(
			staticUnjitteredClip,
			temporal::JitterPixelsToNDC(Vector2(-0.25f, 0.25f), 100, 50));
		const Vector2 jitterOnlyRasterMotion = ComputeTemporalMotionUV(currentRasterClip, previousRasterClip);
		const Vector2 currentUV(0.5f, 0.5f);
		const Vector2 previousRasterUV = temporal::ReprojectToPreviousUV(currentUV, jitterOnlyRasterMotion);
		const Vector2 staticHistoryMotion = ResolveTemporalHistoryMotionUV(jitterOnlyRasterMotion, currentJitterUV, previousJitterUV);
		const Vector2 previousAccumulatedUV = temporal::ReprojectToPreviousUV(currentUV, staticHistoryMotion);
		context.Check(NearlyEqual(jitterOnlyRasterMotion, currentJitterUV - previousJitterUV) &&
			!NearlyEqual(previousRasterUV, currentUV) &&
			NearlyEqual(staticHistoryMotion, Vector2::Zero) &&
			NearlyEqual(previousAccumulatedUV, currentUV),
			"TAA removes raster jitter displacement when reprojecting accumulated history color");

		const Vector2 unjitteredMotionUV(0.03f, -0.02f);
		const Vector2 movingRasterMotionUV =
			unjitteredMotionUV + currentJitterUV - previousJitterUV;
		const Vector2 movingHistoryMotionUV = ResolveTemporalHistoryMotionUV(movingRasterMotionUV, currentJitterUV, previousJitterUV);
		const Vector2 movingPreviousHistoryUV = temporal::ReprojectToPreviousUV(currentUV, movingHistoryMotionUV);
		context.Check(NearlyEqual(movingHistoryMotionUV, unjitteredMotionUV) &&
			NearlyEqual(movingPreviousHistoryUV, currentUV - unjitteredMotionUV),
			"TAA preserves real temporal motion while removing raster jitter displacement");

		const Vector2 currentSkyUV(0.15f, 0.08f);
		const Vector2 previousSkyRasterUV = temporal::ReprojectToPreviousUV(currentSkyUV, jitterOnlyRasterMotion);
		const Vector2 skyRasterMotionUV = currentSkyUV - previousSkyRasterUV;
		const Vector2 skyHistoryMotionUV = ResolveTemporalHistoryMotionUV(skyRasterMotionUV, currentJitterUV, previousJitterUV);
		const Vector2 previousSkyHistoryUV = temporal::ReprojectToPreviousUV(currentSkyUV, skyHistoryMotionUV);
		context.Check(NearlyEqual(skyHistoryMotionUV, Vector2::Zero) &&
			NearlyEqual(previousSkyHistoryUV, currentSkyUV),
			"Static sky reprojection removes jitter after resolving its previous raster UV");

		const Vector2 edgeCurrentUV(0.002f, 0.5f);
		const Vector2 edgePreviousRasterUV = temporal::ReprojectToPreviousUV(edgeCurrentUV, jitterOnlyRasterMotion);
		const Vector2 edgeHistoryMotionUV = ResolveTemporalHistoryMotionUV(jitterOnlyRasterMotion, currentJitterUV, previousJitterUV);
		const Vector2 edgePreviousHistoryUV = temporal::ReprojectToPreviousUV(edgeCurrentUV, edgeHistoryMotionUV);
		context.Check(IsTemporalAAUVInBounds(edgePreviousHistoryUV) &&
			!IsTemporalAAUVInBounds(edgePreviousRasterUV) &&
			!AreTemporalAAReprojectionUVsValid(edgePreviousHistoryUV, edgePreviousRasterUV),
			"TAA rejects history when raw-depth raster UV is out of bounds even if accumulated-history UV is valid");

		context.Check(IsTemporalHistoryDepthCompatible(10.0f, 10.04f) &&
			IsTemporalHistoryDepthCompatible(100.0f, 101.9f) &&
			!IsTemporalHistoryDepthCompatible(10.0f, 10.21f) &&
			!IsTemporalHistoryDepthCompatible(100.0f, 102.1f) &&
			!IsTemporalHistoryDepthCompatible(0.0f, 0.0f) &&
			!IsTemporalHistoryDepthCompatible(std::numeric_limits<float>::quiet_NaN(), 1.0f),
			"Temporal reprojection uses finite positive previous-view Z with max absolute/relative rejection tolerance");

		std::array<TemporalAAGeometryDepthSample, 9> geometryEdgeNeighborhood{};
		geometryEdgeNeighborhood[4] = { 10.0f, false };
		geometryEdgeNeighborhood[5] = { 10.04f, true };
		std::array<TemporalAAGeometryDepthSample, 9> backgroundOnlyNeighborhood{};
		backgroundOnlyNeighborhood[4] = { 10.0f, false };
		std::array<TemporalAAGeometryDepthSample, 9> mismatchedGeometryNeighborhood{};
		mismatchedGeometryNeighborhood[4] = { 10.21f, true };
		context.Check(HasCompatibleTemporalGeometryDepthSample(10.0f, geometryEdgeNeighborhood) &&
			!HasCompatibleTemporalGeometryDepthSample(10.0f, backgroundOnlyNeighborhood) &&
			!HasCompatibleTemporalGeometryDepthSample(10.0f, mismatchedGeometryNeighborhood),
			"TAA geometry depth validation accepts a matching 3x3 geometry neighbor and rejects background or mismatched candidates");

		context.Check(IsTemporalSkyHistoryCompatible(0.0f, DepthConvention::Reversed) &&
			IsTemporalSkyHistoryCompatible(1.0f, DepthConvention::Standard) &&
			!IsTemporalSkyHistoryCompatible(0.5f, DepthConvention::Reversed) &&
			!IsTemporalSkyHistoryCompatible(0.5f, DepthConvention::Standard) &&
			!IsTemporalSkyHistoryCompatible(std::numeric_limits<float>::quiet_NaN(), DepthConvention::Reversed),
			"Sky reprojection accepts only finite previous background depth and rejects previous geometry");

		context.Check(TemporalHistoryInitialAccumulation == 1.0f &&
			TemporalHistoryMaxAccumulation == 255.0f &&
			IsTemporalHistoryAccumulationValid(TemporalHistoryInitialAccumulation) &&
			IsTemporalHistoryAccumulationValid(TemporalHistoryMaxAccumulation) &&
			!IsTemporalHistoryAccumulationValid(0.0f) &&
			!IsTemporalHistoryAccumulationValid(TemporalHistoryMaxAccumulation + 1.0f) &&
			!IsTemporalHistoryAccumulationValid(std::numeric_limits<float>::quiet_NaN()) &&
			ResolveTemporalHistoryNextAge(false, 37.0f) == TemporalHistoryInitialAccumulation &&
			ResolveTemporalHistoryNextAge(true, 0.0f) == TemporalHistoryInitialAccumulation &&
			ResolveTemporalHistoryNextAge(true, TemporalHistoryInitialAccumulation) == 2.0f &&
			ResolveTemporalHistoryNextAge(true, TemporalHistoryMaxAccumulation) == TemporalHistoryMaxAccumulation,
			"Temporal history age starts or resets at one, advances only for accepted valid history, and saturates at the frozen R16Float-exact bound");

		const TemporalAAOutputAlphaContract accumulatedOutputAlphas = ResolveTemporalAAOutputAlphas(37.0f);
		const TemporalAAOutputAlphaContract resetOutputAlphas = ResolveTemporalAAOutputAlphas(std::numeric_limits<float>::quiet_NaN());
		context.Check(accumulatedOutputAlphas.m_ResolvedAlpha == 1.0f &&
			accumulatedOutputAlphas.m_HistoryAlpha == 37.0f &&
			resetOutputAlphas.m_ResolvedAlpha == 1.0f &&
			resetOutputAlphas.m_HistoryAlpha == TemporalHistoryInitialAccumulation,
			"TAA output alpha contract keeps resolved color opaque while history carries a finite bounded age");

		const float previewFeedbackSaturationAge = ResolveTemporalAAFeedbackSaturationAge(0.75f);
		context.Check(previewFeedbackSaturationAge == 3.0f &&
			ResolveTemporalAAFeedbackSaturationAge(0.0f) == 1.0f &&
			ResolveTemporalAAFeedbackSaturationAge(TemporalAADefaultMaxHistoryFeedback) == 33.0f &&
			ResolveTemporalAAFeedbackSaturationAge(TemporalAAMaxHistoryFeedbackCeiling) == 99.0f &&
			ResolveTemporalAAHistoryAgePreview(1.0f, 0.75f) == 0.0f &&
			NearlyEqual(ResolveTemporalAAHistoryAgePreview(3.0f, 0.75f), 2.0f / 3.0f) &&
			ResolveTemporalAAHistoryAgePreview(4.0f, 0.75f) == 1.0f &&
			ResolveTemporalAAHistoryAgePreview(2.0f, 0.0f) == 1.0f &&
			ResolveTemporalAAHistoryAgePreview(std::numeric_limits<float>::quiet_NaN(), 0.75f) == 0.0f,
			"TAA history-age preview maps reset NextAge to black and first cap-affected NextAge to white with the intentional off-by-one convention");

		const TemporalAASettings defaultTemporalAA{};
		static_assert(PackTemporalAAUnitRangePair(0.0f, 1.0f) == 0xffff0000u);
		static_assert(PackTemporalAAUnitRangePair(1.0f, 0.0f) == 0x0000ffffu);
		static_assert(PackTemporalAAUnitRangePair(0.5f, 0.25f) == 0x40008000u);
		static_assert(PackTemporalAAUnitRangePair(TemporalAADepthAbsoluteThreshold, TemporalAADepthRelativeThreshold) == 0x051f0ccdu);
		static_assert(PackTemporalAAMaxHistoryFeedbackAndClampExpansion(
			TemporalAADefaultMaxHistoryFeedback,
			TemporalAADefaultNeighborhoodClampExpansion) == 0x0000f851u);
		static_assert(PackTemporalAAMaxHistoryFeedbackAndClampExpansion(TemporalAAMaxHistoryFeedbackCeiling, 0.0f) == 0x0000fd70u);
		static_assert(PackTemporalAAMaxHistoryFeedbackAndClampExpansion(1.0f, 0.0f) == 0x0000fffeu);
		constexpr std::array<float, 2> packingGolden = UnpackTemporalAAUnitRangePair(0x40008000u);
		constexpr std::array<float, 2> feedbackPackingGolden =
			UnpackTemporalAAUnitRangePair(PackTemporalAAMaxHistoryFeedbackAndClampExpansion(1.0f, 0.0f));
		constexpr std::array<float, 2> feedbackCeilingPackingGolden =
			UnpackTemporalAAUnitRangePair(PackTemporalAAMaxHistoryFeedbackAndClampExpansion(TemporalAAMaxHistoryFeedbackCeiling, 0.0f));
		context.Check(NearlyEqual(packingGolden[0], 32768.0f / 65535.0f) &&
			NearlyEqual(packingGolden[1], 16384.0f / 65535.0f) &&
			feedbackPackingGolden[0] < 1.0f &&
			(feedbackPackingGolden[0] * 65535.0f) == 65534.0f &&
			feedbackCeilingPackingGolden[0] < 1.0f &&
			ResolveTemporalAAFeedbackSaturationAge(feedbackCeilingPackingGolden[0]) == 100.0f &&
			std::ceil(feedbackCeilingPackingGolden[0] / (1.0f - feedbackCeilingPackingGolden[0])) <= TemporalHistoryMaxAccumulation,
			"Temporal AA CPU packing matches the HLSL low/high UNORM16 ABI, feedback can never encode one, and the frozen cap remains reachable before age saturation");

		constexpr std::array<float, 7> historyAges{ 1.0f, 2.0f, 3.0f, 7.0f, 15.0f,
			31.0f, 63.0f };
		constexpr std::array<float, 7> expectedHistoryWeights{ 0.5f, 2.0f / 3.0f,
			0.75f, 0.875f, 0.9375f, 0.96875f,
			TemporalAADefaultMaxHistoryFeedback };
		bool ageWeightGoldenMatches = true;
		for (size_t index = 0; index < historyAges.size(); ++index)
		{
			ageWeightGoldenMatches &= NearlyEqual(
				ResolveTemporalAAHistoryWeight(historyAges[index], 0.0f, 1.0f, 1.0f, defaultTemporalAA),
				expectedHistoryWeights[index]);
		}
		const float staticHistoryWeight = ResolveTemporalAAHistoryWeight(63.0f, 0.0f, 1.0f, 1.0f, defaultTemporalAA);
		const float movingHistoryWeight = ResolveTemporalAAHistoryWeight(63.0f, 5.0f, 1.0f, 1.0f, defaultTemporalAA);
		const float defaultChangedLuminanceWeight = ResolveTemporalAAHistoryWeight(63.0f, 0.0f, 1.0f, 0.9f, defaultTemporalAA);
		TemporalAASettings luminanceResearchSettings = defaultTemporalAA;
		luminanceResearchSettings.m_LuminanceWeightScale = 4.0f;
		const float researchChangedLuminanceWeight = ResolveTemporalAAHistoryWeight(63.0f, 0.0f, 1.0f, 0.9f, luminanceResearchSettings);
		TemporalAASettings packedCeilingSettings = defaultTemporalAA;
		packedCeilingSettings.m_MaxHistoryFeedback = feedbackCeilingPackingGolden[0];
		const float saturatedCeilingWeight = ResolveTemporalAAHistoryWeight(
			TemporalHistoryMaxAccumulation, 0.0f, 1.0f, 1.0f, packedCeilingSettings);
		context.Check(ageWeightGoldenMatches &&
			TemporalAADefaultLuminanceWeightScale == 0.0f &&
			NearlyEqual(staticHistoryWeight, TemporalAADefaultMaxHistoryFeedback) &&
			NearlyEqual(saturatedCeilingWeight, feedbackCeilingPackingGolden[0]) &&
			movingHistoryWeight > 0.0f && movingHistoryWeight < staticHistoryWeight &&
			NearlyEqual(defaultChangedLuminanceWeight, staticHistoryWeight) &&
			researchChangedLuminanceWeight > 0.0f &&
			researchChangedLuminanceWeight < staticHistoryWeight &&
			ResolveTemporalAAHistoryWeight(std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f, 1.0f, defaultTemporalAA) == 0.0f &&
			ResolveTemporalAAHistoryWeight(1.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f, defaultTemporalAA) == 0.0f,
			"Temporal blend follows the age golden sequence, caps at the frozen maximum, retains velocity attenuation, and keeps luminance attenuation research-only by default");

		const float maxHistorySamples = ResolveTemporalAAMaxHistorySamples(TemporalAADefaultMaxHistoryFeedback);
		context.Check(NearlyEqual(maxHistorySamples, TemporalAADefaultMaxHistoryFeedback / (1.0f - TemporalAADefaultMaxHistoryFeedback)) &&
			NearlyEqual(maxHistorySamples / (maxHistorySamples + 1.0f), TemporalAADefaultMaxHistoryFeedback) &&
			ResolveTemporalAAMaxHistorySamples(0.0f) == 0.0f &&
			ResolveTemporalAAMaxHistorySamples(std::numeric_limits<float>::quiet_NaN()) == 0.0f &&
			ResolveTemporalAAMaxHistorySamples(feedbackCeilingPackingGolden[0]) <
			TemporalHistoryMaxAccumulation &&
			ResolveTemporalAAMaxHistorySamples(1.0f) == TemporalHistoryMaxAccumulation &&
			ResolveTemporalHistoryNextSamples(false, 20.0f, 1.0f, maxHistorySamples) == TemporalHistoryInitialAccumulation &&
			ResolveTemporalHistoryNextSamples(true, 0.0f, 1.0f, maxHistorySamples) == TemporalHistoryInitialAccumulation &&
			ResolveTemporalHistoryNextSamples(true, 20.0f,
				std::numeric_limits<float>::quiet_NaN(), maxHistorySamples) == TemporalHistoryInitialAccumulation &&
			ResolveTemporalHistoryNextSamples(true, 1.0f, 1.0f, maxHistorySamples) == 2.0f &&
			ResolveTemporalHistoryNextSamples(true, 20.0f, 0.0f, maxHistorySamples) == TemporalHistoryInitialAccumulation &&
			ResolveTemporalHistoryNextSamples(true, 20.0f, 0.5f, maxHistorySamples) == 11.0f &&
			ResolveTemporalHistoryNextSamples(true, maxHistorySamples, 1.0f, maxHistorySamples) == maxHistorySamples &&
			ResolveTemporalHistoryNextSamples(true, TemporalHistoryMaxAccumulation, 1.0f, maxHistorySamples) == maxHistorySamples &&
			ResolveTemporalAAHistorySamplesPreview(1.0f, maxHistorySamples) == 0.0f &&
			ResolveTemporalAAHistorySamplesPreview(maxHistorySamples, maxHistorySamples) == 1.0f &&
			ResolveTemporalAAHistorySamplesPreview(std::numeric_limits<float>::quiet_NaN(), maxHistorySamples) == 0.0f &&
			ResolveTemporalAAHistorySamplesPreview(1.0f, 1.0f) == 0.0f,
			"Effective samples start or reset at one, discount the carried samples by the history confidence, add one per accepted frame and saturate where their weight meets the feedback ceiling");

		constexpr auto packedLowFeedback = UnpackTemporalAAUnitRangePair(PackTemporalAAMaxHistoryFeedbackAndClampExpansion(0.25f, 0.0f));
		const float lowCapacity = ResolveTemporalAAMaxHistorySamples(packedLowFeedback[0]);
		context.Check(PackTemporalAAMaxHistoryFeedbackAndClampExpansion(0.0f, 0.0f) == 0u &&
			lowCapacity > 0.0f && lowCapacity < TemporalHistoryInitialAccumulation &&
			NearlyEqual(lowCapacity / (lowCapacity + 1.0f), packedLowFeedback[0]) &&
			ResolveTemporalHistoryNextSamples(true, 20.0f, 1.0f, lowCapacity, 0.25f) == TemporalHistoryInitialAccumulation &&
			ResolveTemporalHistoryNextSamples(true, 20.0f, 1.0f, 0.0f, 0.25f) == TemporalHistoryInitialAccumulation,
			"Packed zero and low feedback retain zero or fractional usable capacity without invalidating stored history alpha");

		// After a stretch of half-confidence frames the age keeps counting, so the next
		// confident frame returns to the ceiling weight; the effective samples have
		// settled near 1 / (1 - 0.5) and the weight rises again only as evidence returns.
		float compatibilityAge = TemporalHistoryInitialAccumulation;
		float effectiveSamples = TemporalHistoryInitialAccumulation;
		for (uint32_t frame = 0; frame < 80; ++frame)
		{
			const float confidence = frame < 60 ? 1.0f : 0.5f;
			compatibilityAge = ResolveTemporalHistoryNextAge(true, compatibilityAge);
			effectiveSamples = ResolveTemporalHistoryNextSamples(true, effectiveSamples, confidence, maxHistorySamples);
		}
		const float ageWeightAfterMotion = ResolveTemporalAAHistoryWeight(compatibilityAge, 0.0f, 1.0f, 1.0f, defaultTemporalAA);
		const float samplesWeightAfterMotion = ResolveTemporalAAHistoryWeight(effectiveSamples, 0.0f, 1.0f, 1.0f, defaultTemporalAA);
		context.Check(compatibilityAge == 81.0f &&
			NearlyEqual(ageWeightAfterMotion, TemporalAADefaultMaxHistoryFeedback) &&
			effectiveSamples > 1.9f && effectiveSamples <= 2.0f + 1.0e-4f &&
			samplesWeightAfterMotion < 0.7f,
			"Effective samples keep a low-confidence stretch from returning to the ceiling weight on the next confident frame");

		// Below the display extent a frame counts by the kernel weight of its nearest
		// render sample; a weight of one reduces the weighted terms to the native ones.
		// The static clamp expansion falls to zero with motion and as the disagreement
		// keeps its sign, as through a gradual lighting change.
		constexpr float gaussianKernelScale = 2.29f / (0.75f * 0.75f);
		const Vector2 qualityDisplayPerRender(1.5f, 1.5f);
		const float centredWeight = ResolveTemporalNearestSampleWeight(Vector2(0.0f, 0.0f), qualityDisplayPerRender, gaussianKernelScale);
		const float farWeight = ResolveTemporalNearestSampleWeight(Vector2(0.5f, 0.5f), qualityDisplayPerRender, gaussianKernelScale);
		const float wrappedWeight = ResolveTemporalNearestSampleWeight(Vector2(0.9f, -0.1f), qualityDisplayPerRender, gaussianKernelScale);
		const float farSamples = ResolveTemporalUpscaledSampleWeight(farWeight);
		context.Check(centredWeight == 1.0f && farWeight < 0.02f &&
			NearlyEqual(wrappedWeight, std::exp(-gaussianKernelScale * 0.045f)) &&
			ResolveTemporalUpscaledStaticFraction(0.0f) == 1.0f &&
			ResolveTemporalUpscaledStaticFraction(0.25f) == 0.5f &&
			ResolveTemporalUpscaledStaticFraction(2.0f) == 0.0f &&
			ResolveTemporalUpscaledStaticFraction(std::numeric_limits<float>::quiet_NaN()) == 0.0f &&
			ResolveTemporalUpscaledStaticClampExpansion(0.0f, 0.5f) == 1.0f &&
			ResolveTemporalUpscaledStaticClampExpansion(0.25f, 0.5f) == 0.5f &&
			ResolveTemporalUpscaledStaticClampExpansion(1.0f, 0.5f) == 0.0f &&
			NearlyEqual(ResolveTemporalUpscaledStaticClampExpansion(0.0f, 0.875f), 0.5f) &&
			ResolveTemporalUpscaledStaticClampExpansion(0.0f, 1.0f) == 0.0f &&
			farSamples == farWeight && ResolveTemporalUpscaledSampleWeight(1.0f) == 1.0f &&
			ResolveTemporalUpscaledSampleWeight(0.0f) == TemporalUpscaledMinSampleWeight &&
			NearlyEqual(ComputeTemporalWeightedHistoryWeight(maxHistorySamples, 1.0f,
				maxHistorySamples, 1.0f), ResolveTemporalAAHistoryWeight(maxHistorySamples, 0.0f, 1.0f, 1.0f, defaultTemporalAA)) &&
			ComputeTemporalWeightedHistoryWeight(maxHistorySamples, 1.0f, maxHistorySamples, farSamples) > 0.999f &&
			ComputeTemporalWeightedHistoryWeight(0.0f, 1.0f, maxHistorySamples, 1.0f) == 0.0f &&
			ResolveTemporalHistoryNextSamples(true, 20.0f, 1.0f, maxHistorySamples, 0.25f) == 20.25f &&
			ResolveTemporalHistoryNextSamples(true, 20.0f, 1.0f, maxHistorySamples, 1.0f) ==
			ResolveTemporalHistoryNextSamples(true, 20.0f, 1.0f, maxHistorySamples),
			"Upscaled frames count by their nearest render sample's display-pixel kernel weight, the static clamp expansion withdraws with motion and with a consistent disagreement, and a full sample matches the native weights");

		// A disagreement that alternates in sign relaxes rectification of fully
		// accumulated history; one that keeps its sign, or fresh history, does not.
		const float alternatingConsistency = ResolveTemporalDisagreementConsistency(0.01f, 0.1f);
		const float persistentConsistency = ResolveTemporalDisagreementConsistency(-0.2f, 0.2f);
		TemporalAASettings relaxationOutOfRange{};
		relaxationOutOfRange.m_HistoryRelaxation = 9.0f;
		TemporalAASettings relaxationInvalid{};
		relaxationInvalid.m_HistoryRelaxation = std::numeric_limits<float>::quiet_NaN();
		constexpr std::array<float, 2> relaxationPackingGolden = UnpackTemporalAAUnitRangePair(
			PackTemporalAALuminanceWeightAndHistoryRelaxation(0.0f, 2.0f));
		context.Check(NearlyEqual(alternatingConsistency, 0.1f) &&
			persistentConsistency == 1.0f &&
			ResolveTemporalDisagreementConsistency(0.0f, 0.0f) == 0.0f &&
			ResolveTemporalHistoryRelaxation(1.0f, maxHistorySamples, maxHistorySamples, alternatingConsistency) == 1.0f &&
			ResolveTemporalHistoryRelaxation(1.0f, maxHistorySamples, maxHistorySamples, persistentConsistency) == 0.0f &&
			ResolveTemporalHistoryRelaxation(1.0f, TemporalHistoryInitialAccumulation, maxHistorySamples, alternatingConsistency) == 0.0f &&
			ResolveTemporalHistoryRelaxation(0.0f, maxHistorySamples, maxHistorySamples, alternatingConsistency) == 0.0f &&
			TemporalAASettings{}.m_HistoryRelaxation == TemporalAADefaultHistoryRelaxation &&
			ResolveTemporalAASettings(relaxationOutOfRange).m_HistoryRelaxation == TemporalAAMaxHistoryRelaxation &&
			ResolveTemporalAASettings(relaxationInvalid).m_HistoryRelaxation == TemporalAADefaultHistoryRelaxation &&
			relaxationPackingGolden[0] == 0.0f &&
			NearlyEqual(relaxationPackingGolden[1] * TemporalAAMaxHistoryRelaxation, 2.0f, 1.0e-3f) &&
			TemporalHistoryReliabilityFormat == RHIFormat::R16G16Float,
			"History relaxation widens rectification only for fully accumulated history whose disagreement alternates in sign, within its packed settings range");

		RecordingDevice motionCapabilityDevice;
		motionCapabilityDevice.m_TextureViewsSupported = true;
		const TemporalMotionFormatSupport motionSupport = QueryTemporalMotionFormatSupport(motionCapabilityDevice);
		const RHITextureDesc motionDesc = MakeTemporalMotionTextureDesc(1280, 720);
		context.Check(motionSupport.IsSupported() &&
			motionCapabilityDevice.m_TextureViewQueryCount == 2 &&
			motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Format == TemporalMotionFormat &&
			Test(motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Usage, RHITextureUsage::RenderTarget) &&
			Test(motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Usage,
				RHITextureUsage::Sampled) && motionDesc.m_Usage == RHITextureUsage::None &&
			motionDesc.m_Extent.m_Width == 1280 &&
			motionDesc.m_Extent.m_Height == 720 && motionDesc.m_ClearValue &&
			motionDesc.m_ClearValue->m_Color[0] == 0.0f &&
			motionDesc.m_ClearValue->m_Color[1] == 0.0f,
			"Temporal motion capability requires the combined R16G16Float render-target and SRV contract");

		motionCapabilityDevice.m_TextureViewQueryCount = 0;
		const TemporalAAResolvedColorFormatSupport resolvedColorSupport = QueryTemporalAAResolvedColorFormatSupport(motionCapabilityDevice);
		context.Check(resolvedColorSupport.IsSupported() &&
			motionCapabilityDevice.m_TextureViewQueryCount == 3 &&
			motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Format == TemporalAAResolvedColorFormat &&
			Test(motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Usage, RHITextureUsage::RenderTarget) &&
			Test(motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Usage, RHITextureUsage::Sampled) &&
			Test(motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Usage, RHITextureUsage::UnorderedAccess),
			"Temporal resolved color capability requires the combined HDR RTV, SRV, and typed-UAV contract");

		GraphicsPhysicalPipelineKey depthOnlyMotionCoverage{};
		depthOnlyMotionCoverage.m_BindingLayout = RHIBindingLayoutHandle{ 1, 1 };
		depthOnlyMotionCoverage.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
		depthOnlyMotionCoverage.m_VSId = ShaderID{ 3 };
		depthOnlyMotionCoverage.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		depthOnlyMotionCoverage.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		depthOnlyMotionCoverage.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
		depthOnlyMotionCoverage.m_DepthPreset = DepthPreset::ReversedZWrite;
		depthOnlyMotionCoverage.m_BlendPreset = BlendPreset::ColorWriteDisable;
		GraphicsPhysicalPipelineKey velocityMotionCoverage = depthOnlyMotionCoverage;
		velocityMotionCoverage.m_PSId = ShaderID{ 4 };
		velocityMotionCoverage.m_Formats.m_RenderTargetCount = 1;
		velocityMotionCoverage.m_Formats.m_RenderTargetFormats[0] = TemporalMotionFormat;
		velocityMotionCoverage.m_BlendPreset = BlendPreset::Default;
		const uint64_t opaqueMotionVariant = RenderQueueBuilder::EncodeVariantBits(RenderBucket::Opaque, false);
		const uint64_t alphaMotionVariant = RenderQueueBuilder::EncodeVariantBits(RenderBucket::AlphaTest, false);
		context.Check(
			RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
				depthOnlyMotionCoverage, opaqueMotionVariant) == RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
					velocityMotionCoverage, opaqueMotionVariant) &&
			RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
				depthOnlyMotionCoverage, alphaMotionVariant) == RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
					velocityMotionCoverage, alphaMotionVariant),
			"Velocity MRT preserves opaque and alpha-test depth coverage signatures");

		struct TemporalMotionFixturePassData
		{
			RGTextureId m_Motion{};
		};
		RenderGraph motionGraph({
			.m_Device = &motionCapabilityDevice,
			.m_TransientResourcePool =
				reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		motionGraph.GetBlackboard().Create<RGTemporalGeometryResources>(TemporalGeometryResourcesName);
		motionGraph.AddPass<TemporalMotionFixturePassData>(
			"RenderingContract.TemporalMotionSetup",
			[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
			{
				builder.SideEffect();
				auto& resources = builder.GetBlackboard().Get<
					RGTemporalGeometryResources>(TemporalGeometryResourcesName);
				resources.m_MotionVectors = builder.CreateTexture("Temporal.MotionVectors", MakeTemporalMotionTextureDesc(1280, 720));
				resources.m_MotionSrvDesc = MakeRHITexture2DViewDesc(TemporalMotionFormat);
				resources.m_Width = 1280;
				resources.m_Height = 720;
				data.m_Motion = resources.m_MotionVectors;
			});
		motionGraph.AddPass<TemporalMotionFixturePassData>(
			"View.ClearMotionVectors",
			[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
			{
				builder.SideEffect();
				auto& resources = builder.GetBlackboard().Get<
					RGTemporalGeometryResources>(TemporalGeometryResourcesName);
				builder.WriteInPlace(resources.m_MotionVectors, RGTextureAccess::RenderTarget);
				data.m_Motion = resources.m_MotionVectors;
			});
		motionGraph.AddPass<TemporalMotionFixturePassData>(
			"Geometry.DepthPrepass.Velocity",
			[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
			{
				auto& resources = builder.GetBlackboard().Get<
					RGTemporalGeometryResources>(TemporalGeometryResourcesName);
				builder.ReadWriteInPlace(resources.m_MotionVectors, RGTextureAccess::RenderTarget);
				data.m_Motion = resources.m_MotionVectors;
			});
		motionGraph.AddPass<TemporalMotionFixturePassData>(
			"RenderingContract.TemporalMotionDebugTap",
			[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
			{
				auto& resources = builder.GetBlackboard().Get<
					RGTemporalGeometryResources>(TemporalGeometryResourcesName);
				data.m_Motion = builder.Read(resources.m_MotionVectors, RGTextureAccess::Sample);
				builder.SideEffect();
			});
		const bool motionGraphCompiled = motionGraph.Compile();
		RGSnapshot motionSnapshot;
		if (motionGraphCompiled)
		{
			BuildRenderGraphSnapshot(motionGraph, motionSnapshot);
		}
		const auto motionResource = std::ranges::find(motionSnapshot.m_Resources, "Temporal.MotionVectors", &RGSnapshotResourceInfo::m_Name);
		context.Check(motionGraphCompiled && motionSnapshot.m_Passes.size() == 4 &&
			motionSnapshot.m_Passes[1].m_Name == "View.ClearMotionVectors" &&
			motionSnapshot.m_Passes[1].m_ExecutionOrder <
			motionSnapshot.m_Passes[2].m_ExecutionOrder &&
			motionSnapshot.m_Passes[2].m_ExecutionOrder <
			motionSnapshot.m_Passes[3].m_ExecutionOrder &&
			motionResource != motionSnapshot.m_Resources.end() &&
			motionResource->m_TextureFormat == TemporalMotionFormat &&
			motionResource->m_TextureExtent.m_Width == 1280 &&
			motionResource->m_TextureExtent.m_Height == 720 &&
			Test(static_cast<RHITextureUsage>(motionResource->m_UsageBits), RHITextureUsage::RenderTarget) &&
			Test(static_cast<RHITextureUsage>(motionResource->m_UsageBits), RHITextureUsage::Sampled),
			"Active temporal geometry clears motion before velocity coverage and exposes an explicit sampled debug tap");

		RenderGraph skyOnlyGraph({
			.m_Device = &motionCapabilityDevice,
			.m_TransientResourcePool =
				reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		skyOnlyGraph.GetBlackboard().Create<RGTemporalGeometryResources>(TemporalGeometryResourcesName);
		skyOnlyGraph.AddPass<TemporalMotionFixturePassData>(
			"RenderingContract.SkyOnlyTemporalSetup",
			[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
			{
				builder.SideEffect();
				auto& resources = builder.GetBlackboard().Get<
					RGTemporalGeometryResources>(TemporalGeometryResourcesName);
				resources.m_MotionVectors = builder.CreateTexture("Temporal.SkyOnly.MotionVectors", MakeTemporalMotionTextureDesc(320, 180));
				resources.m_MotionSrvDesc = MakeRHITexture2DViewDesc(TemporalMotionFormat);
				resources.m_Width = 320;
				resources.m_Height = 180;
				data.m_Motion = resources.m_MotionVectors;
			});
		skyOnlyGraph.AddPass<TemporalMotionFixturePassData>(
			"View.ClearMotionVectors",
			[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
			{
				auto& resources = builder.GetBlackboard().Get<
					RGTemporalGeometryResources>(TemporalGeometryResourcesName);
				builder.WriteInPlace(resources.m_MotionVectors, RGTextureAccess::RenderTarget);
				data.m_Motion = resources.m_MotionVectors;
			});
		skyOnlyGraph.AddPass<TemporalMotionFixturePassData>(
			"PostProcess.TemporalAA",
			[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
			{
				auto& resources = builder.GetBlackboard().Get<
					RGTemporalGeometryResources>(TemporalGeometryResourcesName);
				data.m_Motion = builder.Read(resources.m_MotionVectors, RGTextureAccess::Sample);
				builder.SideEffect();
			});
		const bool skyOnlyGraphCompiled = skyOnlyGraph.Compile();
		RGSnapshot skyOnlySnapshot;
		if (skyOnlyGraphCompiled)
		{
			BuildRenderGraphSnapshot(skyOnlyGraph, skyOnlySnapshot);
		}
		context.Check(skyOnlyGraphCompiled && skyOnlySnapshot.m_Passes.size() == 3 &&
			skyOnlySnapshot.m_Passes[1].m_Name == "View.ClearMotionVectors" &&
			!skyOnlySnapshot.m_Passes[1].m_Culled &&
			skyOnlySnapshot.m_Passes[1].m_ExecutionOrder <
			skyOnlySnapshot.m_Passes[2].m_ExecutionOrder,
			"Zero-draw sky-only temporal frames retain motion clear before reprojection resolve");

		context.Check(IsTemporalColorCompatible(TemporalColorAbi::LinearRec709SceneReferredV1, PostProcessColorState::SceneLinearRec709, 1.0f) &&
			!IsTemporalColorCompatible(TemporalColorAbi::LinearRec709SceneReferredV1, PostProcessColorState::DisplayLinearRec709, 1.0f) &&
			!IsTemporalColorCompatible(TemporalColorAbi::LinearRec709SceneReferredV1, PostProcessColorState::SceneLinearRec709, 0.5f) &&
			IsTemporalColorCompatible(TemporalColorAbi::LinearRec709PreExposedV2, PostProcessColorState::SceneLinearRec709, 0.5f),
			"V1 remains unit-only while V2 accepts pre-exposed scene-linear Rec.709");

		Camera rigCamera(Camera::CreateInfo{});
		CameraController rigController(CameraController::CreateInfo{});
		CameraRig cameraRig;
		cameraRig.AttachMainCamera(rigCamera, rigController);
		const CameraRig::EffectiveDisplayView mainDisplayView = cameraRig.ResolveEffectiveDisplayView();
		const size_t debugCameraIndex = cameraRig.AddDebugCameraFromActive();
		const CameraRig::CameraSlot* debugCameraSlot = cameraRig.GetCameraSlot(debugCameraIndex);
		const bool selectedDebugView = debugCameraSlot && cameraRig.SetDisplayViewId(debugCameraSlot->m_RenderViewId);
		const CameraRig::EffectiveDisplayView debugDisplayView = cameraRig.ResolveEffectiveDisplayView();
		const CameraRenderViewQueryBase& routingQuery = cameraRig;
		const auto copiedVisibility = routingQuery.GetRenderViewVisibilityMode(debugDisplayView.m_ViewId);
		context.Check(routingQuery.GetDisplayViewId() == debugDisplayView.m_ViewId &&
			copiedVisibility.has_value() &&
			!routingQuery.GetRenderViewVisibilityMode(RenderViewID::Unknown).has_value(),
			"Camera routing query observes selected views and distinguishes missing slots");
		CameraRig::CameraSlot* mutableDebugCameraSlot = cameraRig.GetCameraSlot(debugCameraIndex);
		if (mutableDebugCameraSlot)
		{
			mutableDebugCameraSlot->m_EnableRenderView = false;
			mutableDebugCameraSlot->m_VisibilityMode = RenderViewVisibilityMode::None;
		}
		const CameraRig::EffectiveDisplayView fallbackDisplayView = cameraRig.ResolveEffectiveDisplayView();
		context.Check(mainDisplayView.IsValid() &&
			mainDisplayView.m_ViewId == RenderViewID::Main && selectedDebugView &&
			debugDisplayView.IsValid() &&
			IsDebugCameraRenderViewID(debugDisplayView.m_ViewId) &&
			fallbackDisplayView.IsValid() &&
			fallbackDisplayView.m_ViewId == RenderViewID::Main,
			"CameraRig resolves the effective display view and fallback exactly once");
		context.Check(copiedVisibility.has_value() &&
			*copiedVisibility != RenderViewVisibilityMode::None &&
			routingQuery.GetRenderViewVisibilityMode(debugDisplayView.m_ViewId) == RenderViewVisibilityMode::None &&
			routingQuery.GetDisplayViewId() == debugDisplayView.m_ViewId,
			"Camera routing observations are copied and preserve requested display selection during fallback");

		const uint64_t initialResetSerial = rigCamera.GetTemporalResetSerial();
		rigCamera.SetPosition(Vector3(1.0f, 2.0f, 3.0f));
		rigCamera.SetFov(55.0f);
		const bool continuousChangesPreserveSerial = rigCamera.GetTemporalResetSerial() == initialResetSerial;
		rigCamera.RequestTemporalReset();
		const uint64_t explicitResetSerial = rigCamera.GetTemporalResetSerial();
		rigCamera.LookAt(Vector3(2.0f, 3.0f, 4.0f), Vector3::Zero);
		context.Check(continuousChangesPreserveSerial &&
			explicitResetSerial == initialResetSerial + 1 &&
			rigCamera.GetTemporalResetSerial() == explicitResetSerial + 1,
			"Camera temporal reset serial changes only for explicit discontinuities and cuts");

		ViewRenderProfile profile{};
		const Camera camera(Camera::CreateInfo{});
		const ResolvedViewRenderSettings defaultSettings = ResolveViewRenderSettings(profile, camera);
		profile.m_Lighting.m_EnableAerialPerspective = false;
		const auto baselineSettings = ResolveViewRenderSettings(profile, camera);
		context.Check(defaultSettings.m_Lighting.m_EnableAerialPerspective &&
			!baselineSettings.m_Lighting.m_EnableAerialPerspective &&
			baselineSettings.m_Exposure.m_PreExposure == defaultSettings.m_Exposure.m_PreExposure &&
			baselineSettings.m_Lighting.m_GTAO.m_Enabled == defaultSettings.m_Lighting.m_GTAO.m_Enabled &&
			baselineSettings.m_TemporalAA.m_Enabled == defaultSettings.m_TemporalAA.m_Enabled,
			"Aerial baseline switch resolves independently of exposure, AO and TAA");
		profile.m_TemporalAA.m_Enabled = true;
		const ResolvedViewRenderSettings enabledSettings = ResolveViewRenderSettings(profile, camera);
		profile.m_TemporalAA.m_DepthAbsoluteThreshold = -1.0f;
		profile.m_TemporalAA.m_DepthRelativeThreshold = 2.0f;
		profile.m_TemporalAA.m_MaxHistoryFeedback = 2.0f;
		profile.m_TemporalAA.m_VelocityWeightScale = -1.0f;
		profile.m_TemporalAA.m_LuminanceWeightScale = 32.0f;
		profile.m_TemporalAA.m_NeighborhoodClampExpansion = std::numeric_limits<float>::quiet_NaN();
		const ResolvedViewRenderSettings clampedSettings = ResolveViewRenderSettings(profile, camera);
		context.Check(!defaultSettings.m_TemporalAA.m_Enabled &&
			enabledSettings.m_TemporalAA.m_Enabled &&
			NearlyEqual(enabledSettings.m_TemporalAA.m_DepthAbsoluteThreshold, TemporalAADepthAbsoluteThreshold) &&
			NearlyEqual(enabledSettings.m_TemporalAA.m_DepthRelativeThreshold, TemporalAADepthRelativeThreshold) &&
			NearlyEqual(enabledSettings.m_TemporalAA.m_MaxHistoryFeedback, TemporalAADefaultMaxHistoryFeedback) &&
			NearlyEqual(enabledSettings.m_TemporalAA.m_VelocityWeightScale, TemporalAADefaultVelocityWeightScale) &&
			NearlyEqual(enabledSettings.m_TemporalAA.m_LuminanceWeightScale, TemporalAADefaultLuminanceWeightScale) &&
			NearlyEqual(enabledSettings.m_TemporalAA.m_NeighborhoodClampExpansion, TemporalAADefaultNeighborhoodClampExpansion) &&
			NearlyEqual(clampedSettings.m_TemporalAA.m_DepthAbsoluteThreshold, 0.0f) &&
			NearlyEqual(clampedSettings.m_TemporalAA.m_DepthRelativeThreshold, TemporalAAMaxDepthThreshold) &&
			NearlyEqual(clampedSettings.m_TemporalAA.m_MaxHistoryFeedback, TemporalAAMaxHistoryFeedbackCeiling) &&
			clampedSettings.m_TemporalAA.m_MaxHistoryFeedback < 1.0f &&
			NearlyEqual(clampedSettings.m_TemporalAA.m_VelocityWeightScale, 0.0f) &&
			NearlyEqual(clampedSettings.m_TemporalAA.m_LuminanceWeightScale, TemporalAAMaxLuminanceWeightScale) &&
			NearlyEqual(clampedSettings.m_TemporalAA.m_NeighborhoodClampExpansion, 0.0f),
			"Temporal AA authoring defaults off, feedback resolves strictly below one, and quality controls resolve to finite bounded frame settings");

		const TemporalAACapabilityStatus fullCapabilities{
			.m_MotionRenderTarget = true,
			.m_MotionShaderResource = true,
			.m_ResolvedColorRenderTarget = true,
			.m_ResolvedColorShaderResource = true,
			.m_ResolvedColorTypedUavStore = true,
			.m_HistoryColorShaderResource = true,
			.m_HistoryColorTypedUavStore = true,
			.m_HistoryDepthShaderResource = true,
			.m_HistoryDepthTypedUavStore = true,
			.m_HistoryReliabilityShaderResource = true,
			.m_HistoryReliabilityTypedUavStore = true,
		};
		context.Check(fullCapabilities.IsCoreAvailable() &&
			!TemporalAACapabilityStatus{}.IsCoreAvailable(),
			"Temporal AA core capability requires the complete device texture support");

		TemporalFramePlanResolveInfo resolveInfo{
			.m_TemporalAA = {
				.m_Settings = { .m_Enabled = true },
				.m_Capabilities = fullCapabilities,
			},
			.m_DisplayViewId = RenderViewID::Main,
			.m_SceneExtensionParticipation = SceneExtensionTemporalParticipation::PostTAA,
			.m_ResetIdentity = 17,
			.m_SessionIdentity = 23,
			.m_DisplayViewEligible = true,
			.m_DepthVelocityPathAvailable = true,
		};
		const ResolvedTemporalFramePlan activePlan = ResolveTemporalFramePlan(resolveInfo);
		TemporalFramePlanResolveInfo disabledInfo = resolveInfo;
		disabledInfo.m_TemporalAA.m_Settings.m_Enabled = false;
		const ResolvedTemporalFramePlan disabledPlan = ResolveTemporalFramePlan(disabledInfo);
		TemporalFramePlanResolveInfo missingCoreInfo = resolveInfo;
		missingCoreInfo.m_TemporalAA.m_Capabilities = {};
		const ResolvedTemporalFramePlan missingCorePlan = ResolveTemporalFramePlan(missingCoreInfo);
		TemporalFramePlanResolveInfo ineligibleInfo = resolveInfo;
		ineligibleInfo.m_DisplayViewEligible = false;
		const ResolvedTemporalFramePlan ineligiblePlan = ResolveTemporalFramePlan(ineligibleInfo);
		TemporalFramePlanResolveInfo missingDepthVelocityInfo = resolveInfo;
		missingDepthVelocityInfo.m_DepthVelocityPathAvailable = false;
		const ResolvedTemporalFramePlan missingDepthVelocityPlan = ResolveTemporalFramePlan(missingDepthVelocityInfo);
		TemporalFramePlanResolveInfo unsupportedExtensionInfo = resolveInfo;
		unsupportedExtensionInfo.m_SceneExtensionParticipation = SceneExtensionTemporalParticipation::TemporalUnsupported;
		const ResolvedTemporalFramePlan unsupportedExtensionPlan = ResolveTemporalFramePlan(unsupportedExtensionInfo);
		const auto temporalAAOf = [](const ResolvedTemporalFramePlan& plan) noexcept
		{
			return plan.GetConsumer(TemporalConsumer::TemporalAA);
		};
		const auto unavailableBecause = [&]
		(const ResolvedTemporalFramePlan& plan, TemporalConsumerDisableReason reason) noexcept
		{
			return temporalAAOf(plan).m_Requested &&
				temporalAAOf(plan).m_Status == TemporalConsumerStatus::Unavailable &&
				temporalAAOf(plan).m_DisableReason == reason &&
				plan.m_Services == TemporalService::None;
		};
		context.Check(temporalAAOf(activePlan).IsActive() && activePlan.m_TemporalAA.m_CoreAvailable &&
			temporalAAOf(activePlan).m_DisableReason == TemporalConsumerDisableReason::None &&
			activePlan.m_ResetIdentity == 17 && activePlan.m_SessionIdentity == 23 &&
			!temporalAAOf(disabledPlan).IsActive() &&
			!temporalAAOf(disabledPlan).m_Requested &&
			temporalAAOf(disabledPlan).m_Status == TemporalConsumerStatus::Disabled &&
			temporalAAOf(disabledPlan).m_DisableReason == TemporalConsumerDisableReason::NotRequested &&
			unavailableBecause(missingCorePlan, TemporalConsumerDisableReason::CoreCapabilityUnavailable) &&
			unavailableBecause(ineligiblePlan, TemporalConsumerDisableReason::DisplayViewIneligible) &&
			unavailableBecause(missingDepthVelocityPlan, TemporalConsumerDisableReason::DepthVelocityPathUnavailable) &&
			unavailableBecause(unsupportedExtensionPlan, TemporalConsumerDisableReason::SceneExtensionUnsupported),
			"Temporal AA consumer resolves one atomic active state and preserves every disable cause");

		constexpr TemporalService allServices = TemporalService::ProjectionJitter |
			TemporalService::GeometryMotion | TemporalService::FrameContinuity |
			TemporalService::ColorDepthHistory;
		TemporalFramePlanResolveInfo referenceInfo = disabledInfo;
		referenceInfo.m_ReferenceRequested = true;
		const ResolvedTemporalFramePlan referencePlan = ResolveTemporalFramePlan(referenceInfo);
		const TemporalConsumerPlan& reference = referencePlan.GetConsumer(TemporalConsumer::Reference);
		context.Check(activePlan.m_Services == allServices &&
			temporalAAOf(activePlan).m_Services == allServices &&
			activePlan.GetProjectionJitterOwner() == TemporalConsumer::TemporalAA &&
			!activePlan.GetConsumer(TemporalConsumer::Reference).m_Requested &&
			disabledPlan.m_Services == TemporalService::None &&
			!disabledPlan.GetProjectionJitterOwner() &&
			referencePlan.m_Services == TemporalService::ProjectionJitter &&
			reference.m_Requested && reference.IsActive() &&
			reference.m_Services == TemporalService::ProjectionJitter &&
			referencePlan.GetProjectionJitterOwner() == TemporalConsumer::Reference &&
			!temporalAAOf(referencePlan).IsActive() &&
			!referencePlan.HasService(TemporalService::GeometryMotion) &&
			!referencePlan.HasService(TemporalService::FrameContinuity) &&
			!referencePlan.HasService(TemporalService::ColorDepthHistory),
			"A frame enables exactly the union of its active consumers' services; with no "
			"active consumer it enables none, and only a jitter-removing consumer owns jitter");

		TemporalFramePlanResolveInfo ambientOcclusionInfo = disabledInfo;
		ambientOcclusionInfo.m_AmbientOcclusionRequested = true;
		ambientOcclusionInfo.m_AmbientOcclusionAvailable = true;
		const ResolvedTemporalFramePlan ambientOcclusionPlan = ResolveTemporalFramePlan(ambientOcclusionInfo);
		TemporalFramePlanResolveInfo combinedInfo = resolveInfo;
		combinedInfo.m_AmbientOcclusionRequested = true;
		combinedInfo.m_AmbientOcclusionAvailable = true;
		const ResolvedTemporalFramePlan combinedPlan = ResolveTemporalFramePlan(combinedInfo);
		TemporalFramePlanResolveInfo unsupportedAmbientOcclusionInfo = ambientOcclusionInfo;
		unsupportedAmbientOcclusionInfo.m_AmbientOcclusionAvailable = false;
		const ResolvedTemporalFramePlan unsupportedAmbientOcclusionPlan = ResolveTemporalFramePlan(unsupportedAmbientOcclusionInfo);
		TemporalFramePlanResolveInfo ineligibleAmbientOcclusionInfo = ambientOcclusionInfo;
		ineligibleAmbientOcclusionInfo.m_DisplayViewEligible = false;
		const ResolvedTemporalFramePlan ineligibleAmbientOcclusionPlan = ResolveTemporalFramePlan(ineligibleAmbientOcclusionInfo);
		const auto ambientOcclusionOf = [](const ResolvedTemporalFramePlan& plan) noexcept
		{
			return plan.GetConsumer(TemporalConsumer::AmbientOcclusion);
		};
		constexpr TemporalService ambientOcclusionServices =
			TemporalService::GeometryMotion | TemporalService::FrameContinuity;
		context.Check(ambientOcclusionOf(ambientOcclusionPlan).IsActive() &&
			ambientOcclusionPlan.m_Services == ambientOcclusionServices &&
			!ambientOcclusionPlan.GetProjectionJitterOwner() &&
			ambientOcclusionPlan.m_TemporalAA.m_ResolutionPreset == TemporalAAResolutionPreset::Native &&
			combinedPlan.m_Services == allServices &&
			combinedPlan.GetProjectionJitterOwner() == TemporalConsumer::TemporalAA &&
			ambientOcclusionOf(combinedPlan).IsActive() &&
			temporalAAOf(combinedPlan).IsActive() &&
			ambientOcclusionOf(unsupportedAmbientOcclusionPlan).m_Status == TemporalConsumerStatus::Unavailable &&
			ambientOcclusionOf(unsupportedAmbientOcclusionPlan).m_DisableReason == TemporalConsumerDisableReason::CoreCapabilityUnavailable &&
			unsupportedAmbientOcclusionPlan.m_Services == TemporalService::None &&
			ambientOcclusionOf(ineligibleAmbientOcclusionPlan).m_DisableReason == TemporalConsumerDisableReason::DisplayViewIneligible &&
			!ambientOcclusionOf(disabledPlan).m_Requested &&
			ambientOcclusionOf(disabledPlan).m_DisableReason == TemporalConsumerDisableReason::NotRequested &&
			ambientOcclusionPlan.UsesAmbientOcclusionHistory() &&
			!combinedPlan.UsesAmbientOcclusionHistory() &&
			!unsupportedAmbientOcclusionPlan.UsesAmbientOcclusionHistory(),
			"Temporal GTAO consumes motion and continuity without jitter, joins Temporal AA "
			"as a service union, keeps its own history only without the resolve and reports "
			"every disable cause");

		TemporalFramePlanResolveInfo qualityInfo = resolveInfo;
		qualityInfo.m_TemporalAA.m_Settings.m_ResolutionPreset = TemporalAAResolutionPreset::Quality;
		TemporalFramePlanResolveInfo upscalingInfo = qualityInfo;
		upscalingInfo.m_TemporalAA.m_UpscalingAvailable = true;
		TemporalFramePlanResolveInfo disabledUpscalingInfo = upscalingInfo;
		disabledUpscalingInfo.m_TemporalAA.m_Settings.m_Enabled = false;
		TemporalFramePlanResolveInfo referenceUpscalingInfo = disabledUpscalingInfo;
		referenceUpscalingInfo.m_ReferenceRequested = true;
		const ResolvedTemporalFramePlan upscalingPlan = ResolveTemporalFramePlan(upscalingInfo);
		context.Check(
			ResolveTemporalFramePlan(qualityInfo).m_TemporalAA.m_ResolutionPreset == TemporalAAResolutionPreset::Native &&
			upscalingPlan.m_TemporalAA.m_ResolutionPreset == TemporalAAResolutionPreset::Quality &&
			upscalingPlan.m_TemporalAA.GetJitterSequenceLength() == 18 &&
			activePlan.m_TemporalAA.GetJitterSequenceLength() == 8 &&
			ResolveTemporalFramePlan(disabledUpscalingInfo).m_TemporalAA.m_ResolutionPreset == TemporalAAResolutionPreset::Native &&
			ResolveTemporalFramePlan(referenceUpscalingInfo).m_TemporalAA.m_ResolutionPreset == TemporalAAResolutionPreset::Native,
			"A render scale below native applies only to an active Temporal AA consumer "
			"whose pipeline resolve can upscale");

		TemporalFramePlanResolveInfo ageInfo = resolveInfo;
		ageInfo.m_TemporalAA.m_Settings.m_HistoryAccumulation = TemporalAAHistoryAccumulation::CompatibilityAge;
		TemporalFramePlanResolveInfo disabledAgeInfo = ageInfo;
		disabledAgeInfo.m_TemporalAA.m_Settings.m_Enabled = false;
		TemporalAASettings invalidAccumulation{};
		invalidAccumulation.m_HistoryAccumulation = static_cast<TemporalAAHistoryAccumulation>(7);
		context.Check(TemporalAASettings{}.m_HistoryAccumulation == TemporalAAHistoryAccumulation::EffectiveSamples &&
			activePlan.m_TemporalAA.m_HistoryAccumulation == TemporalAAHistoryAccumulation::EffectiveSamples &&
			ResolveTemporalFramePlan(ageInfo).m_TemporalAA.m_HistoryAccumulation == TemporalAAHistoryAccumulation::CompatibilityAge &&
			ResolveTemporalFramePlan(disabledAgeInfo).m_TemporalAA.m_HistoryAccumulation == TemporalAAHistoryAccumulation::EffectiveSamples &&
			ResolveTemporalAASettings(invalidAccumulation).m_HistoryAccumulation == TemporalAAHistoryAccumulation::EffectiveSamples &&
			GetTemporalAAHistoryAccumulationName(TemporalAAHistoryAccumulation::CompatibilityAge) == "compatibility-age" &&
			GetTemporalAAHistoryAccumulationName(TemporalAAHistoryAccumulation::EffectiveSamples) == "effective-samples",
			"The frame plan carries the requested accumulation model of an active Temporal AA consumer");

		const ViewResolution quality720 = ResolveTemporalAAViewResolution({ 1280, 720 }, TemporalAAResolutionPreset::Quality);
		const ViewResolution quality1080 = ResolveTemporalAAViewResolution({ 1920, 1080 }, TemporalAAResolutionPreset::Quality);
		context.Check(
			ResolveTemporalAAViewResolution({ 1280, 720 }, TemporalAAResolutionPreset::Native) == ResolveNativeViewResolution({ 1280, 720 }) &&
			quality720.m_Render == ViewExtent{ 853, 480 } &&
			quality720.m_Display == ViewExtent{ 1280, 720 } && !quality720.IsNative() &&
			quality1080.m_Render == ViewExtent{ 1280, 720 } &&
			ResolveTemporalAAViewResolution({ 1, 1 }, TemporalAAResolutionPreset::Quality).m_Render == ViewExtent{ 1, 1 },
			"Quality renders at the nearest 2/3 of each display dimension, never below a pixel");

		bool qualityJitterExtendsNative = true;
		bool qualityJitterInsidePixel = true;
		bool qualityJitterDistinct = true;
		constexpr uint32_t qualityLength = GetTemporalAAJitterSequenceLength(TemporalAAResolutionPreset::Quality);
		for (uint32_t index = 0; index < qualityLength; ++index)
		{
			const Vector2 sample = temporal::GetJitterSamplePixels(index, qualityLength);
			if (index < temporal::JitterSampleCount)
			{
				const Vector2 native = temporal::GetJitterSamplePixels(index);
				qualityJitterExtendsNative &=
					sample.m_X == native.m_X && sample.m_Y == native.m_Y;
			}
			qualityJitterInsidePixel &= sample.m_X >= -0.5f && sample.m_X < 0.5f &&
				sample.m_Y >= -0.5f && sample.m_Y < 0.5f;
			for (uint32_t other = 0; other < index; ++other)
			{
				const Vector2 previous = temporal::GetJitterSamplePixels(other, qualityLength);
				qualityJitterDistinct &=
					previous.m_X != sample.m_X || previous.m_Y != sample.m_Y;
			}
		}
		const Vector2 wrapped = temporal::GetJitterSamplePixels(qualityLength, qualityLength);
		const Vector2 first = temporal::GetJitterSamplePixels(0, qualityLength);
		context.Check(qualityJitterExtendsNative && qualityJitterInsidePixel &&
			qualityJitterDistinct && wrapped.m_X == first.m_X && wrapped.m_Y == first.m_Y,
			"The Quality jitter sequence extends the native Halton(2,3) phases to 18 distinct "
			"in-pixel samples and wraps at its length");

		context.Check(IsTemporalAADisplayViewEligible(RenderViewID::Main, 1920, 1080) &&
			IsTemporalAADisplayViewEligible(RenderViewID::DebugCamera0, 1, 1) &&
			!IsTemporalAADisplayViewEligible(RenderViewID::DirectionalShadow, 1920, 1080) &&
			!IsTemporalAADisplayViewEligible(RenderViewID::Main, 0, 1080),
			"Temporal AA eligibility is restricted to non-empty perspective display views");

		RenderPipelineForwardPlus forwardPipeline;
		const ResolvedTemporalFramePlan forwardPlan = forwardPipeline.ResolveTemporalFramePlan(resolveInfo);
		RenderPipelineForwardPlus integratedExtensionPipeline({
			.m_SceneExtension = std::make_unique<IntegratedTemporalSceneExtension>(),
		});
		TemporalFramePlanResolveInfo noDepthVelocityClaimInfo = resolveInfo;
		noDepthVelocityClaimInfo.m_DepthVelocityPathAvailable = false;
		const ResolvedTemporalFramePlan forwardWithoutClaimPlan = forwardPipeline.ResolveTemporalFramePlan(noDepthVelocityClaimInfo);
		TemporalFramePlanResolveInfo integratedExtensionInfo = resolveInfo;
		integratedExtensionInfo.m_DepthVelocityPathAvailable = true;
		const ResolvedTemporalFramePlan integratedExtensionPlan = integratedExtensionPipeline.ResolveTemporalFramePlan(integratedExtensionInfo);
		context.Check(temporalAAOf(forwardPlan).IsActive() &&
			forwardPlan.m_DepthVelocityPathAvailable &&
			temporalAAOf(forwardWithoutClaimPlan).IsActive() &&
			forwardPlan.m_SceneExtensionParticipation == SceneExtensionTemporalParticipation::PostTAA &&
			temporalAAOf(forwardPlan).m_DisableReason == TemporalConsumerDisableReason::None &&
			!temporalAAOf(integratedExtensionPlan).IsActive() &&
			integratedExtensionPlan.m_SceneExtensionParticipation == SceneExtensionTemporalParticipation::TemporalUnsupported,
			"The Forward+ pipeline exposes its velocity path and rejects unsupported integrated extensions");
		context.Check(forwardPipeline.ResolveTemporalFramePlan(qualityInfo).m_TemporalAA.m_ResolutionPreset ==
			TemporalAAResolutionPreset::Quality &&
			ResolveTemporalFramePlan(qualityInfo).m_TemporalAA.m_ResolutionPreset == TemporalAAResolutionPreset::Native,
			"The Forward+ resolve upscales a requested Quality preset; a pipeline without an "
			"upscaling resolve renders native");
		TemporalFramePlanResolveInfo diagnosticInfo = qualityInfo;
		diagnosticInfo.m_NativeResolutionRequired = true;
		const ResolvedTemporalFramePlan diagnosticPlan = forwardPipeline.ResolveTemporalFramePlan(diagnosticInfo);
		TemporalFramePlanResolveInfo nativeDiagnosticInfo = diagnosticInfo;
		nativeDiagnosticInfo.m_TemporalAA.m_Settings.m_ResolutionPreset = TemporalAAResolutionPreset::Native;
		context.Check(diagnosticPlan.IsConsumerActive(TemporalConsumer::TemporalAA) &&
			diagnosticPlan.m_TemporalAA.m_ResolutionPreset == TemporalAAResolutionPreset::Native &&
			forwardPipeline.ResolveTemporalFramePlan(nativeDiagnosticInfo).m_TemporalAA.m_ResolutionPreset ==
			TemporalAAResolutionPreset::Native &&
			forwardPipeline.ResolveTemporalFramePlan(qualityInfo).m_TemporalAA.m_ResolutionPreset == TemporalAAResolutionPreset::Quality,
			"Current-frame diagnostic demand enters Native immediately and returns to Quality without a stale-frame latch");
		diagnosticInfo.m_TemporalAA.m_UpscalingAvailable = true;
		context.Check(ResolveTemporalFramePlan(diagnosticInfo).m_TemporalAA.m_ResolutionPreset == TemporalAAResolutionPreset::Native,
			"The shared planner honors Native composition requirements even when upscaling is available");

		Renderer renderer;
		context.Check(!renderer.GetTemporalAACapabilityStatus().IsCoreAvailable(),
			"Renderer publishes temporal core capability as unavailable before initialization");

		const ResolvedTemporalFramePlan viewPlan = ResolveTemporalFramePlan({
			.m_TemporalAA = {
				.m_Settings = enabledSettings.m_TemporalAA,
				.m_Capabilities = {},
			},
			.m_DisplayViewId = RenderViewID::Main,
			.m_SceneExtensionParticipation = SceneExtensionTemporalParticipation::PostTAA,
			.m_DisplayViewEligible = true,
		});
		RenderViewBuilder viewBuilder;
		RenderView view = viewBuilder.Build<RenderViewID::Main>({
			.m_Camera = camera,
			.m_RenderSettings = defaultSettings,
			.m_TemporalFramePlan = viewPlan,
			.m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }),
		});
		TemporalViewHistory unavailableViewHistory{};
		TemporalObjectHistory unavailableObjectHistory{};
		TemporalFrameTransaction unavailableTransaction;
		unavailableTransaction.Begin(unavailableViewHistory, unavailableObjectHistory, viewPlan, ResolveNativeViewResolution({ 1920, 1080 }));
		unavailableTransaction.PrepareDisplayView(view);
		unavailableTransaction.CommitCompleted();
		const float nearRawDepth = ProjectPosition(Vector3(0.0f, 0.0f, view.m_Near), view.m_RasterProj).m_RawDepth;
		const float farRawDepth = ProjectPosition(Vector3(0.0f, 0.0f, view.m_Far), view.m_RasterProj).m_RawDepth;
		context.Check(viewPlan.GetConsumer(TemporalConsumer::TemporalAA).m_Requested &&
			!viewPlan.m_TemporalAA.m_CoreAvailable &&
			!viewPlan.IsConsumerActive(TemporalConsumer::TemporalAA) &&
			viewPlan.GetConsumer(TemporalConsumer::TemporalAA).m_DisableReason == TemporalConsumerDisableReason::CoreCapabilityUnavailable &&
			viewPlan.m_Services == TemporalService::None &&
			unavailableTransaction.GetState() == TemporalFrameTransactionState::Committed &&
			NearlyEqual(unavailableTransaction.GetJitterPixels(), Vector2::Zero) &&
			!unavailableTransaction.ParticipatedInResolve() &&
			!unavailableViewHistory.m_Valid &&
			unavailableObjectHistory.GetDiagnostics().m_EntryCount == 0 &&
			view.m_UnjitteredProj.ToArray() == view.m_RasterProj.ToArray() &&
			view.m_UnjitteredViewProj.ToArray() == view.m_RasterViewProj.ToArray() &&
			view.m_JitterPixels.m_X == 0.0f && view.m_JitterPixels.m_Y == 0.0f &&
			view.m_JitterUV.m_X == 0.0f && view.m_JitterUV.m_Y == 0.0f &&
			!view.m_HasPreviousTemporalState &&
			NearlyEqual(screen_space::RawDepthToPositiveViewZ(nearRawDepth,
				view.m_DepthReconstructionParams, view.m_DepthConvention), view.m_Near) &&
			NearlyEqual(screen_space::RawDepthToPositiveViewZ(farRawDepth, view.m_DepthReconstructionParams, view.m_DepthConvention), view.m_Far,
				PositionTolerance * view.m_Far),
			"Core-unavailable temporal frames disable jitter, history participation, and resolve atomically");

		const Vector2 jitterPixels(0.5f, -0.25f);
		const Vector2 jitterUV = temporal::JitterPixelsToUV(jitterPixels, 100, 50);
		const Vector2 jitterNDC = temporal::JitterPixelsToNDC(jitterPixels, 100, 50);
		const Vector4 jitteredClip = temporal::ApplyJitterToClipPosition(Vector4(1.0f, 2.0f, 3.0f, 2.0f), jitterNDC);
		context.Check(NearlyEqual(jitterUV, Vector2(0.005f, -0.005f)) &&
			NearlyEqual(jitterNDC, Vector2(0.01f, 0.01f)) &&
			NearlyEqual(jitteredClip.m_X, 1.02f) &&
			NearlyEqual(jitteredClip.m_Y, 2.02f) &&
			NearlyEqual(temporal::ReprojectToPreviousUV(Vector2(0.5f, 0.5f), Vector2(0.1f, -0.2f)), Vector2(0.4f, 0.7f)),
			"CPU temporal math fixes pixel, NDC, clip, and motion reprojection signs");

		constexpr std::array<Vector2, temporal::JitterSampleCount> expectedJitterSamples =
		{
			Vector2(0.0f, -1.0f / 6.0f),
			Vector2(-1.0f / 4.0f, 1.0f / 6.0f),
			Vector2(1.0f / 4.0f, -7.0f / 18.0f),
			Vector2(-3.0f / 8.0f, -1.0f / 18.0f),
			Vector2(1.0f / 8.0f, 5.0f / 18.0f),
			Vector2(-1.0f / 8.0f, -5.0f / 18.0f),
			Vector2(3.0f / 8.0f, 1.0f / 18.0f),
			Vector2(-7.0f / 16.0f, 7.0f / 18.0f),
		};
		bool jitterGoldenTableMatches = true;
		for (uint32_t index = 0; index < temporal::JitterSampleCount; ++index)
		{
			jitterGoldenTableMatches = jitterGoldenTableMatches &&
				NearlyEqual(temporal::GetJitterSamplePixels(index), expectedJitterSamples[index]);
		}
		context.Check(jitterGoldenTableMatches,
			"Temporal jitter sequence matches the exact Halton(2,3) eight-sample table");

		TemporalViewHistory temporalViewHistory{};
		TemporalObjectHistory temporalObjectHistory{};
		TemporalFrameTransaction abortedTransaction;
		abortedTransaction.Begin(temporalViewHistory, temporalObjectHistory, activePlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView abortedView = viewBuilder.Build<RenderViewID::Main>({
			.m_Camera = camera,
			.m_RenderSettings = enabledSettings,
			.m_TemporalFramePlan = activePlan,
			.m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }),
		});
		abortedTransaction.PrepareDisplayView(abortedView);
		const Vector4 unjitteredClip = math::Transform(Vector4(1.0f, 2.0f, 3.0f, 1.0f), abortedView.m_UnjitteredProj);
		const Vector4 rasterClip = math::Transform(Vector4(1.0f, 2.0f, 3.0f, 1.0f), abortedView.m_RasterProj);
		const Vector4 expectedRasterClip = temporal::ApplyJitterToClipPosition(
			unjitteredClip,
			temporal::JitterPixelsToNDC(abortedTransaction.GetJitterPixels(), 1920, 1080));
		abortedTransaction.Abort();

		TemporalFrameTransaction committedTransaction;
		committedTransaction.Begin(temporalViewHistory, temporalObjectHistory, activePlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView committedView = viewBuilder.Build<RenderViewID::Main>({
			.m_Camera = camera,
			.m_RenderSettings = enabledSettings,
			.m_TemporalFramePlan = activePlan,
			.m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }),
		});
		committedTransaction.PrepareDisplayView(committedView);
		committedTransaction.MarkResolveParticipated();
		committedTransaction.CommitCompleted();

		TemporalFrameTransaction noResolveTransaction;
		noResolveTransaction.Begin(temporalViewHistory, temporalObjectHistory, activePlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView noResolveView = viewBuilder.Build<RenderViewID::Main>({
			.m_Camera = camera,
			.m_RenderSettings = enabledSettings,
			.m_TemporalFramePlan = activePlan,
			.m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }),
		});
		noResolveTransaction.PrepareDisplayView(noResolveView);
		noResolveTransaction.CommitCompleted();

		ResolvedTemporalFramePlan changedSessionPlan = activePlan;
		++changedSessionPlan.m_SessionIdentity;
		TemporalFrameTransaction changedSessionTransaction;
		changedSessionTransaction.Begin(
			temporalViewHistory, temporalObjectHistory, changedSessionPlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView changedSessionView = viewBuilder.Build<RenderViewID::Main>({
			.m_Camera = camera,
			.m_RenderSettings = enabledSettings,
			.m_TemporalFramePlan = changedSessionPlan,
			.m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }),
		});
		changedSessionTransaction.PrepareDisplayView(changedSessionView);
		changedSessionTransaction.Abort();
		const RenderView temporalShadowView =
			viewBuilder.Build<RenderViewID::DirectionalShadow>({
				.m_MainView = noResolveView,
			});

		TemporalFrameTransaction fatalTransaction;
		fatalTransaction.Begin(temporalViewHistory, temporalObjectHistory, activePlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView fatalView = viewBuilder.Build<RenderViewID::Main>({
			.m_Camera = camera,
			.m_RenderSettings = enabledSettings,
			.m_TemporalFramePlan = activePlan,
			.m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }),
		});
		fatalTransaction.PrepareDisplayView(fatalView);
		fatalTransaction.MarkResolveParticipated();
		fatalTransaction.InvalidateAfterFatal();
		context.Check(abortedTransaction.GetJitterIndex() == 0 &&
			committedTransaction.GetJitterIndex() == 0 &&
			noResolveTransaction.GetJitterIndex() == 1 &&
			noResolveView.m_HasPreviousTemporalState &&
			noResolveView.m_PreviousJitterUV.m_X == committedView.m_JitterUV.m_X &&
			noResolveView.m_PreviousJitterUV.m_Y == committedView.m_JitterUV.m_Y &&
			noResolveTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
			changedSessionTransaction.GetJitterIndex() == 0 &&
			!changedSessionView.m_HasPreviousTemporalState &&
			temporalShadowView.m_RasterProj.ToArray() == temporalShadowView.m_UnjitteredProj.ToArray() &&
			fatalTransaction.GetState() == TemporalFrameTransactionState::Invalidated &&
			!temporalViewHistory.m_Valid && temporalViewHistory.m_NextJitterIndex == 0 &&
			NearlyEqual(rasterClip.m_X, expectedRasterClip.m_X) &&
			NearlyEqual(rasterClip.m_Y, expectedRasterClip.m_Y) &&
			NearlyEqual(rasterClip.m_Z, expectedRasterClip.m_Z) &&
			NearlyEqual(rasterClip.m_W, expectedRasterClip.m_W),
			"Temporal frame transaction advances only on committed resolve and invalidates on fatal");
		context.Check(abortedTransaction.GetFrameIndex() == 0 &&
			committedView.m_TemporalFrameIndex == 0 &&
			noResolveTransaction.GetFrameIndex() == 1 &&
			noResolveView.m_TemporalFrameIndex == 1 &&
			changedSessionView.m_TemporalFrameIndex == 0 &&
			temporalShadowView.m_TemporalFrameIndex == 0 &&
			temporalViewHistory.m_NextFrameIndex == 0,
			"The temporal frame index counts committed frames since the history reset and "
			"restarts with it");

		// Temporal GTAO advances its sampling sequence with the committed frame index.
		ResolvedViewRenderSettings ambientOcclusionSettings = enabledSettings;
		ambientOcclusionSettings.m_TemporalAA.m_Enabled = false;
		ambientOcclusionSettings.m_Lighting.m_GTAO.m_TemporalAccumulation = true;
		TemporalViewHistory ambientOcclusionViewHistory{};
		TemporalObjectHistory ambientOcclusionObjectHistory{};
		const auto runAmbientOcclusionFrame = [&](bool submitted) noexcept
		{
			TemporalFrameTransaction transaction;
			transaction.Begin(ambientOcclusionViewHistory, ambientOcclusionObjectHistory,
				ambientOcclusionPlan, ResolveNativeViewResolution({ 1920, 1080 }));
			RenderView view = viewBuilder.Build<RenderViewID::Main>({
				.m_Camera = camera,
				.m_RenderSettings = ambientOcclusionSettings,
				.m_TemporalFramePlan = ambientOcclusionPlan,
				.m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }),
			});
			transaction.PrepareDisplayView(view);
			const bool unjittered = view.m_RasterProj.ToArray() == view.m_UnjitteredProj.ToArray();
			if (submitted)
			{
				transaction.CommitCompleted();
			}
			else
			{
				transaction.Abort();
			}
			return std::pair{ view.m_TemporalFrameIndex, unjittered };
		};
		const auto firstAmbientOcclusionFrame = runAmbientOcclusionFrame(true);
		const auto abortedAmbientOcclusionFrame = runAmbientOcclusionFrame(false);
		const auto repeatedAmbientOcclusionFrame = runAmbientOcclusionFrame(true);
		const auto nextAmbientOcclusionFrame = runAmbientOcclusionFrame(true);
		context.Check(firstAmbientOcclusionFrame == std::pair{ 0u, true } &&
			abortedAmbientOcclusionFrame == std::pair{ 1u, true } &&
			repeatedAmbientOcclusionFrame == std::pair{ 1u, true } &&
			nextAmbientOcclusionFrame == std::pair{ 2u, true },
			"Temporal GTAO frames advance the sampling index only after submission and "
			"leave the raster projection unjittered");

		TemporalFramePlanResolveInfo biasedInfo = resolveInfo;
		biasedInfo.m_TemporalAA.m_Settings.m_TextureLodBiasOffset = -0.75f;
		const ResolvedTemporalFramePlan biasedPlan = ResolveTemporalFramePlan(biasedInfo);
		TemporalFramePlanResolveInfo biasedQualityInfo = biasedInfo;
		biasedQualityInfo.m_TemporalAA.m_Settings.m_ResolutionPreset = TemporalAAResolutionPreset::Quality;
		biasedQualityInfo.m_TemporalAA.m_UpscalingAvailable = true;
		const ResolvedTemporalFramePlan biasedQualityPlan = ResolveTemporalFramePlan(biasedQualityInfo);
		TemporalFramePlanResolveInfo biasedInactiveInfo = biasedInfo;
		biasedInactiveInfo.m_TemporalAA.m_Settings.m_Enabled = false;
		const ResolvedTemporalFramePlan biasedInactivePlan = ResolveTemporalFramePlan(biasedInactiveInfo);
		struct DisplayViewSampling
		{
			float m_BuiltViewBias = 0.0f;
			float m_PreparedViewBias = 0.0f;
			float m_TransactionBias = 0.0f;
			uint32_t m_JitterSequenceLength = 0;
		};
		const auto resolveDisplayViewSampling = [&](const ResolvedTemporalFramePlan& plan,
			std::optional<TemporalReferenceSample> referenceSample = std::nullopt) noexcept
		{
			const ViewResolution resolution = ResolveTemporalAAViewResolution({ 1920, 1080 }, plan.m_TemporalAA.m_ResolutionPreset);
			TemporalViewHistory viewHistory{};
			TemporalObjectHistory objectHistory{};
			TemporalFrameTransaction transaction;
			transaction.Begin(viewHistory, objectHistory, plan, resolution, nullptr, SceneColorStoragePreExposureV1, referenceSample);
			RenderView view = viewBuilder.Build<RenderViewID::Main>({
				.m_Camera = camera,
				.m_RenderSettings = enabledSettings,
				.m_TemporalFramePlan = plan,
				.m_Resolution = resolution,
			});
			DisplayViewSampling sampling{ .m_BuiltViewBias = view.m_TextureLodBias };
			transaction.PrepareDisplayView(view);
			sampling.m_PreparedViewBias = view.m_TextureLodBias;
			sampling.m_TransactionBias = transaction.GetTextureLodBias();
			sampling.m_JitterSequenceLength = transaction.GetJitterSequenceLength();
			transaction.Abort();
			return sampling;
		};
		const DisplayViewSampling nativeSampling = resolveDisplayViewSampling(biasedPlan);
		const DisplayViewSampling qualitySampling = resolveDisplayViewSampling(biasedQualityPlan);
		const DisplayViewSampling inactiveSampling = resolveDisplayViewSampling(biasedInactivePlan);
		const DisplayViewSampling ambientOcclusionSampling = resolveDisplayViewSampling(ambientOcclusionPlan);
		const DisplayViewSampling referenceSampling = resolveDisplayViewSampling(referencePlan,
			TemporalReferenceSample{ .m_Index = 2, .m_Count = 16, .m_TextureLodBias = -2.0f });
		TemporalAASettings outOfRangeBias{};
		outOfRangeBias.m_TextureLodBiasOffset = -5.0f;
		context.Check(biasedPlan.m_TemporalAA.m_TextureLodBiasOffset == -0.75f &&
			biasedInactivePlan.m_TemporalAA.m_TextureLodBiasOffset == 0.0f &&
			NearlyEqual(qualitySampling.m_TransactionBias, std::log2(1280.0f / 1920.0f) - 0.75f) &&
			qualitySampling.m_PreparedViewBias == qualitySampling.m_TransactionBias &&
			qualitySampling.m_JitterSequenceLength == 18,
			"The texture LOD bias adds log2(render / display) to the temporal offset");
		context.Check(nativeSampling.m_TransactionBias == -0.75f &&
			nativeSampling.m_PreparedViewBias == -0.75f &&
			nativeSampling.m_JitterSequenceLength == 8 &&
			referenceSampling.m_TransactionBias == -2.0f &&
			referenceSampling.m_PreparedViewBias == -2.0f &&
			referenceSampling.m_JitterSequenceLength == 16 &&
			inactiveSampling.m_PreparedViewBias == 0.0f &&
			inactiveSampling.m_JitterSequenceLength == 0 &&
			ambientOcclusionSampling.m_PreparedViewBias == 0.0f &&
			ambientOcclusionSampling.m_JitterSequenceLength == 0 &&
			nativeSampling.m_BuiltViewBias == 0.0f && referenceSampling.m_BuiltViewBias == 0.0f &&
			temporalShadowView.m_TextureLodBias == 0.0f &&
			ResolveTemporalAASettings(outOfRangeBias).m_TextureLodBiasOffset == TemporalAAMinTextureLodBiasOffset,
			"The jitter owner selects the display view's texture LOD bias and jitter sequence "
			"length once in the temporal frame transaction; other frames and views carry none");

		TemporalViewHistory submittedViewHistory{};
		TemporalObjectHistory submittedObjectHistory{};
		const auto buildSubmittedHistoryView = [&]() noexcept
		{
			return viewBuilder.Build<RenderViewID::Main>({
				.m_Camera = camera,
				.m_RenderSettings = enabledSettings,
				.m_TemporalFramePlan = activePlan,
				.m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }),
			});
		};
		const RenderObjectHistoryKey objectHistoryKey{
			.m_EntityIdentity = 7,
			.m_ModelId = ModelID{ 11 },
			.m_ModelContentGeneration = 3,
			.m_ModelMeshIndex = 2,
			.m_MeshId = MeshID{ 13 },
			.m_MeshContentGeneration = 5,
			.m_SessionIdentity = activePlan.m_SessionIdentity,
		};
		const Matrix initialObjectModel = math::CreateTranslation(Vector3(1.0f, 2.0f, 3.0f));
		const Matrix movedObjectModel = math::CreateTranslation(Vector3(4.0f, 5.0f, 6.0f));

		TemporalFrameTransaction initialObjectTransaction;
		initialObjectTransaction.Begin(submittedViewHistory, submittedObjectHistory, activePlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView initialObjectView = buildSubmittedHistoryView();
		initialObjectTransaction.PrepareDisplayView(initialObjectView);
		const Matrix initialPreviousModel = initialObjectTransaction.ResolvePreviousObjectModel(objectHistoryKey, initialObjectModel);
		const bool initialObjectStaged = initialObjectTransaction.StageSubmittedObject(objectHistoryKey, initialObjectModel);
		initialObjectTransaction.MarkResolveParticipated();
		initialObjectTransaction.CommitCompleted();
		const TemporalCommittedObjectState* initialCommittedObject = submittedObjectHistory.Find(objectHistoryKey);
		const bool initialObjectCommitted = initialCommittedObject &&
			initialCommittedObject->m_Model.ToArray() == initialObjectModel.ToArray() &&
			initialCommittedObject->m_LastSeenCommittedFrame == 1;

		TemporalFrameTransaction abortedObjectTransaction;
		abortedObjectTransaction.Begin(submittedViewHistory, submittedObjectHistory, activePlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView abortedObjectView = buildSubmittedHistoryView();
		abortedObjectTransaction.PrepareDisplayView(abortedObjectView);
		const Matrix abortedPreviousModel = abortedObjectTransaction.ResolvePreviousObjectModel(objectHistoryKey, movedObjectModel);
		const bool abortedObjectStaged = abortedObjectTransaction.StageSubmittedObject(objectHistoryKey, movedObjectModel);
		abortedObjectTransaction.CommitCompleted();
		const TemporalCommittedObjectState* committedAfterAbort = submittedObjectHistory.Find(objectHistoryKey);
		const bool objectAbortPreserved = committedAfterAbort &&
			committedAfterAbort->m_Model.ToArray() == initialObjectModel.ToArray() &&
			committedAfterAbort->m_LastSeenCommittedFrame == 1;

		TemporalFrameTransaction movedObjectTransaction;
		movedObjectTransaction.Begin(submittedViewHistory, submittedObjectHistory, activePlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView movedObjectView = buildSubmittedHistoryView();
		movedObjectTransaction.PrepareDisplayView(movedObjectView);
		const Matrix movedPreviousModel = movedObjectTransaction.ResolvePreviousObjectModel(objectHistoryKey, movedObjectModel);
		const bool movedObjectStaged = movedObjectTransaction.StageSubmittedObject(objectHistoryKey, movedObjectModel);
		movedObjectTransaction.MarkResolveParticipated();
		movedObjectTransaction.CommitCompleted();

		TemporalFrameTransaction abortedDisappearanceTransaction;
		abortedDisappearanceTransaction.Begin(submittedViewHistory,
			submittedObjectHistory, activePlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView abortedDisappearanceView = buildSubmittedHistoryView();
		abortedDisappearanceTransaction.PrepareDisplayView(abortedDisappearanceView);
		abortedDisappearanceTransaction.Abort();
		const TemporalCommittedObjectState* committedAfterAbortedDisappearance = submittedObjectHistory.Find(objectHistoryKey);
		const bool abortedDisappearancePreserved =
			committedAfterAbortedDisappearance &&
			committedAfterAbortedDisappearance->m_Model.ToArray() == movedObjectModel.ToArray() &&
			committedAfterAbortedDisappearance->m_LastSeenCommittedFrame == 2;

		RenderObjectHistoryKey replacementKey = objectHistoryKey;
		replacementKey.m_ModelId = ModelID{ 12 };
		replacementKey.m_ModelContentGeneration = 1;
		RenderObjectHistoryKey republishedGeometryKey = objectHistoryKey;
		++republishedGeometryKey.m_MeshContentGeneration;
		const Matrix replacementModel = math::CreateTranslation(Vector3(8.0f, 9.0f, 10.0f));
		RenderObjectHistoryKey reusedEntityKey = objectHistoryKey;
		++reusedEntityKey.m_EntityIdentity;
		TemporalFrameTransaction replacementTransaction;
		replacementTransaction.Begin(submittedViewHistory, submittedObjectHistory, activePlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView replacementView = buildSubmittedHistoryView();
		replacementTransaction.PrepareDisplayView(replacementView);
		const Matrix replacementPreviousModel = replacementTransaction.ResolvePreviousObjectModel(replacementKey, replacementModel);
		const Matrix reusedEntityPreviousModel = replacementTransaction.ResolvePreviousObjectModel(reusedEntityKey, replacementModel);
		const Matrix republishedGeometryPreviousModel =
			replacementTransaction.ResolvePreviousObjectModel(republishedGeometryKey, replacementModel);
		const bool replacementStaged = replacementTransaction.StageSubmittedObject(replacementKey, replacementModel);
		replacementTransaction.MarkResolveParticipated();
		replacementTransaction.CommitCompleted();
		const TemporalObjectHistoryDiagnostics replacementDiagnostics = submittedObjectHistory.GetDiagnostics();
		const TemporalCommittedObjectState* committedReplacement = submittedObjectHistory.Find(replacementKey);
		const bool replacementRetiredOldIdentity =
			!submittedObjectHistory.Find(objectHistoryKey) && committedReplacement &&
			committedReplacement->m_Model.ToArray() == replacementModel.ToArray();

		ResolvedTemporalFramePlan replacementSessionPlan = activePlan;
		++replacementSessionPlan.m_SessionIdentity;
		RenderObjectHistoryKey replacementSessionKey = replacementKey;
		replacementSessionKey.m_SessionIdentity = replacementSessionPlan.m_SessionIdentity;
		TemporalFrameTransaction replacementSessionTransaction;
		replacementSessionTransaction.Begin(submittedViewHistory, submittedObjectHistory,
			replacementSessionPlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView replacementSessionView = viewBuilder.Build<RenderViewID::Main>({
			.m_Camera = camera,
			.m_RenderSettings = enabledSettings,
			.m_TemporalFramePlan = replacementSessionPlan,
			.m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }),
		});
		replacementSessionTransaction.PrepareDisplayView(replacementSessionView);
		const Matrix replacementSessionPrevious =
			replacementSessionTransaction.ResolvePreviousObjectModel(replacementSessionKey, initialObjectModel);
		replacementSessionTransaction.Abort();

		ResolvedTemporalFramePlan resetObjectPlan = activePlan;
		++resetObjectPlan.m_ResetIdentity;
		TemporalFrameTransaction resetObjectTransaction;
		resetObjectTransaction.Begin(submittedViewHistory, submittedObjectHistory, resetObjectPlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView resetObjectView = viewBuilder.Build<RenderViewID::Main>({
			.m_Camera = camera,
			.m_RenderSettings = enabledSettings,
			.m_TemporalFramePlan = resetObjectPlan,
			.m_Resolution = ResolveNativeViewResolution({ 1920, 1080 }),
		});
		resetObjectTransaction.PrepareDisplayView(resetObjectView);
		const Matrix resetPreviousModel = resetObjectTransaction.ResolvePreviousObjectModel(replacementKey, initialObjectModel);
		resetObjectTransaction.Abort();

		TemporalFrameTransaction fatalObjectTransaction;
		fatalObjectTransaction.Begin(submittedViewHistory, submittedObjectHistory, activePlan, ResolveNativeViewResolution({ 1920, 1080 }));
		RenderView fatalObjectView = buildSubmittedHistoryView();
		fatalObjectTransaction.PrepareDisplayView(fatalObjectView);
		fatalObjectTransaction.InvalidateAfterFatal();
		const TemporalObjectHistoryDiagnostics fatalObjectDiagnostics = submittedObjectHistory.GetDiagnostics();
		context.Check(objectHistoryKey.IsValid() && initialObjectStaged &&
			abortedObjectStaged && movedObjectStaged && replacementStaged &&
			abortedObjectTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
			initialPreviousModel.ToArray() == initialObjectModel.ToArray() &&
			initialObjectCommitted &&
			abortedPreviousModel.ToArray() == initialObjectModel.ToArray() &&
			objectAbortPreserved &&
			movedPreviousModel.ToArray() == initialObjectModel.ToArray() &&
			abortedDisappearancePreserved &&
			replacementPreviousModel.ToArray() == replacementModel.ToArray() &&
			reusedEntityPreviousModel.ToArray() == replacementModel.ToArray() &&
			republishedGeometryPreviousModel.ToArray() == replacementModel.ToArray() &&
			replacementRetiredOldIdentity &&
			replacementDiagnostics.m_EntryCount == 1 &&
			replacementDiagnostics.m_Capacity == MaxObjectCapacity &&
			replacementDiagnostics.m_LastCommittedFrame == 3 &&
			replacementSessionPrevious.ToArray() == initialObjectModel.ToArray() &&
			resetPreviousModel.ToArray() == initialObjectModel.ToArray() &&
			fatalObjectDiagnostics.m_EntryCount == 0 &&
			fatalObjectDiagnostics.m_LastCommittedFrame == 0,
			"Submitted object history commits atomically, rejects stale identities, and "
			"retires only on committed disappearance");

		RenderFrameBuildResult frameResult{};
		frameResult.m_TemporalFramePlan = activePlan;
		const RenderFrameContext frameContext = frameResult.MakeRenderFrameContext();
		context.Check(frameContext.GetTemporalFramePlan() == activePlan,
			"RenderFrameContext preserves the single pre-frame temporal plan without re-resolution");
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingTemporalAASelfTests(SelfTestContext& context) noexcept
	{
		RunTemporalCompatibilityAndHistoryContractTests(context);
	}
}
