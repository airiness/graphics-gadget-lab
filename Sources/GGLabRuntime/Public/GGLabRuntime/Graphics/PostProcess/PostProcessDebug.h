#pragma once

#include <cstdint>

namespace gglab
{
	inline constexpr uint32_t MaxBloomPyramidLevels = 8;

	// Each inspector owns a preview request and a persistent output. The default
	// PostProcessing channel also serves Lab actions that select diagnostic taps.
	enum class PostProcessPreviewChannel : uint8_t
	{
		PostProcessing,
		AmbientOcclusion,
		TemporalAA,
		Atmosphere,
		SceneDepth,
		GlobalIllumination,
		Count
	};

	inline constexpr float PostProcessPreviewMaximumExposureEV = 8.0f;
	[[nodiscard]] constexpr float GetPostProcessPreviewMinimumExposureEV(
		PostProcessPreviewChannel channel) noexcept
	{
		return channel == PostProcessPreviewChannel::GlobalIllumination ? -20.0f : -8.0f;
	}

	enum class PostProcessDebugTap : uint8_t
	{
		SceneColor = 0,
		BloomPrefilter = 1,
		BloomPyramid = 2,
		BloomResult = 3,
		SceneDepthRaw = 4,
		SceneDepthLinearViewZ = 5,
		GTAORawAO = 6,
		GTAOHalfDepthViewZ = 7,
		GTAOReconstructedNormal = 8,
		GTAOSelectedSurfaceOffset = 9,
		GTAODenoiseX = 10,
		GTAODenoiseY = 11,
		GTAOFinalAO = 12,
		GTAOAOOnlyLightingContribution = 13,
		TemporalMotionDirection = 14,
		TemporalMotionMagnitude = 15,
		TemporalHistoryColor = 16,
		TemporalReprojectionUV = 17,
		TemporalRejection = 18,
		TemporalHistoryWeight = 19,
		TemporalHistorySamples = 20,

		AtmosphereTransmittance = 21,
		AtmosphereMultipleScattering = 22,
		AtmosphereSkyView = 23,
		AtmosphereAerialTransmittance = 24,
		AtmosphereAerialInScattering = 25,

		// How far rectification moved accepted history, relative to the neighborhood box.
		TemporalClipDistance = 26,
		// Fraction of the configured history relaxation that widened the neighborhood box.
		TemporalHistoryRelaxation = 27,
		// Effective sample count of GTAO's own visibility history over its configured maximum;
		// available only while temporal GTAO keeps that history.
		GTAOHistorySamples = 28,

		GIIrradiance = 29,
		GILambertRadiance = 30,
		GISunIrradiance = 31,
		GIEnvironmentIrradiance = 32,
		GIBaseVisibility = 33,
		GICoatVisibility = 34,
		GICombinedVisibility = 35,

		Count = 36
	};
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::SceneColor) == 0);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::BloomPrefilter) == 1);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::BloomPyramid) == 2);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::BloomResult) == 3);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::SceneDepthRaw) == 4);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::SceneDepthLinearViewZ) == 5);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAORawAO) == 6);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAOHalfDepthViewZ) == 7);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAOReconstructedNormal) == 8);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAOSelectedSurfaceOffset) == 9);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAODenoiseX) == 10);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAODenoiseY) == 11);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAOFinalAO) == 12);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAOAOOnlyLightingContribution) == 13);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::TemporalMotionDirection) == 14);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::TemporalMotionMagnitude) == 15);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::TemporalHistoryColor) == 16);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::TemporalReprojectionUV) == 17);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::TemporalRejection) == 18);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::TemporalHistoryWeight) == 19);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::TemporalHistorySamples) == 20);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::TemporalClipDistance) == 26);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::TemporalHistoryRelaxation) == 27);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAOHistorySamples) == 28);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::Count) == 36);

	[[nodiscard]] constexpr bool IsGIDiffusePreviewTap(PostProcessDebugTap tap) noexcept
	{
		return tap == PostProcessDebugTap::GIIrradiance ||
			tap == PostProcessDebugTap::GILambertRadiance ||
			tap == PostProcessDebugTap::GISunIrradiance ||
			tap == PostProcessDebugTap::GIEnvironmentIrradiance;
	}

	[[nodiscard]] constexpr bool IsGIVisibilityPreviewTap(PostProcessDebugTap tap) noexcept
	{
		return tap == PostProcessDebugTap::GIBaseVisibility ||
			tap == PostProcessDebugTap::GICoatVisibility ||
			tap == PostProcessDebugTap::GICombinedVisibility;
	}

	struct PostProcessDebugSelection
	{
		PostProcessDebugTap m_Tap = PostProcessDebugTap::BloomResult;
		uint32_t m_BloomPyramidLevel = 0;

		bool operator==(const PostProcessDebugSelection&) const noexcept = default;
	};
}
