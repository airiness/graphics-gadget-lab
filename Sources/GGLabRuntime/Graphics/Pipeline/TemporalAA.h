#pragma once

#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "Graphics/ScreenSpace/ScreenSpaceTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gglab
{
	inline constexpr float TemporalAADepthAbsoluteThreshold = 0.05f;
	inline constexpr float TemporalAADepthRelativeThreshold = 0.02f;
	inline constexpr float TemporalAADefaultVelocityWeightScale = 0.05f;
	inline constexpr float TemporalAADefaultLuminanceWeightScale = 0.0f;
	inline constexpr float TemporalAADefaultNeighborhoodClampExpansion = 0.0f;
	inline constexpr float TemporalHistoryInitialAge = 1.0f;
	inline constexpr float TemporalHistoryMaxAge = 255.0f;
	inline constexpr float TemporalAADefaultMaxHistoryFeedback = 0.97f;
	inline constexpr float TemporalAAMaxHistoryFeedbackCeiling = 0.99f;
	inline constexpr float TemporalAAMaxDepthThreshold = 1.0f;
	inline constexpr float TemporalAAMaxVelocityWeightScale = 1.0f;
	inline constexpr float TemporalAAMaxLuminanceWeightScale = 16.0f;
	inline constexpr float TemporalAAMaxNeighborhoodClampExpansion = 1.0f;
	inline constexpr uint32_t TemporalAAUnitRangePairMask = 0xffffu;
	inline constexpr float TemporalAAUnitRangeQuantizationScale =
		static_cast<float>(TemporalAAUnitRangePairMask);
	static_assert(TemporalHistoryMaxAge <= 2048.0f);
	static_assert(TemporalAADefaultMaxHistoryFeedback >= 0.0f &&
		TemporalAADefaultMaxHistoryFeedback < TemporalAAMaxHistoryFeedbackCeiling);
	static_assert(TemporalAAMaxHistoryFeedbackCeiling < 1.0f);
	static_assert(TemporalAAMaxHistoryFeedbackCeiling <=
		TemporalHistoryMaxAge / (TemporalHistoryMaxAge + 1.0f));

	[[nodiscard]] inline bool IsTemporalHistoryAgeValid(float historyAge) noexcept
	{
		return std::isfinite(historyAge) && historyAge >= TemporalHistoryInitialAge &&
			historyAge <= TemporalHistoryMaxAge;
	}

	[[nodiscard]] inline float ResolveTemporalHistoryNextAge(
		bool historyAccepted, float previousHistoryAge) noexcept
	{
		return historyAccepted && IsTemporalHistoryAgeValid(previousHistoryAge)
			? std::min(previousHistoryAge + 1.0f, TemporalHistoryMaxAge)
			: TemporalHistoryInitialAge;
	}

	struct TemporalAAOutputAlphaContract final
	{
		float m_ResolvedAlpha = 1.0f;
		float m_HistoryAlpha = TemporalHistoryInitialAge;
	};

	[[nodiscard]] inline TemporalAAOutputAlphaContract ResolveTemporalAAOutputAlphas(
		float nextHistoryAge) noexcept
	{
		return {
			.m_ResolvedAlpha = 1.0f,
			.m_HistoryAlpha = IsTemporalHistoryAgeValid(nextHistoryAge)
				? nextHistoryAge
				: TemporalHistoryInitialAge,
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

	[[nodiscard]] inline bool AreTemporalAAReprojectionUVsValid(
		const Vector2& previousHistoryUV, const Vector2& previousRasterUV) noexcept
	{
		return IsTemporalAAUVInBounds(previousHistoryUV) &&
			IsTemporalAAUVInBounds(previousRasterUV);
	}

	[[nodiscard]] constexpr uint32_t QuantizeTemporalAAUnitRange(float value) noexcept
	{
		return static_cast<uint32_t>(
			std::clamp(value, 0.0f, 1.0f) * TemporalAAUnitRangeQuantizationScale + 0.5f);
	}

	[[nodiscard]] constexpr uint32_t PackTemporalAAUnitRangePair(
		float low, float high) noexcept
	{
		return QuantizeTemporalAAUnitRange(low) |
			(QuantizeTemporalAAUnitRange(high) << 16u);
	}

	[[nodiscard]] constexpr uint32_t PackTemporalAAMaxHistoryFeedbackAndClampExpansion(
		float maxHistoryFeedback, float clampExpansion) noexcept
	{
		const uint32_t feedback = std::min(
			QuantizeTemporalAAUnitRange(maxHistoryFeedback),
			TemporalAAUnitRangePairMask - 1u);
		return feedback | (QuantizeTemporalAAUnitRange(clampExpansion) << 16u);
	}

	[[nodiscard]] constexpr std::array<float, 2> UnpackTemporalAAUnitRangePair(
		uint32_t packedValues) noexcept
	{
		return {
			static_cast<float>(packedValues & TemporalAAUnitRangePairMask) /
				TemporalAAUnitRangeQuantizationScale,
			static_cast<float>(packedValues >> 16u) /
				TemporalAAUnitRangeQuantizationScale,
		};
	}

	[[nodiscard]] inline float ResolveTemporalAAFeedbackSaturationAge(
		float maxHistoryFeedback) noexcept
	{
		if (!std::isfinite(maxHistoryFeedback))
		{
			return TemporalHistoryInitialAge;
		}

		const float maxPackedFeedback =
			static_cast<float>(TemporalAAUnitRangePairMask - 1u) /
			TemporalAAUnitRangeQuantizationScale;
		const float feedback = std::clamp(maxHistoryFeedback, 0.0f, maxPackedFeedback);
		float saturationAge = std::max(std::ceil(feedback / std::max(
			1.0f - feedback, 1.0f / TemporalAAUnitRangeQuantizationScale)),
			TemporalHistoryInitialAge);
		// Correct ceil() at exactly representable discrete weights such as 99 / 100.
		// The diagnostic must report the first integer age whose actual float weight
		// reaches the cap, rather than inheriting division-rounding error from the
		// closed-form estimate.
		while (saturationAge > TemporalHistoryInitialAge &&
			(saturationAge - 1.0f) / saturationAge >= feedback)
		{
			saturationAge -= 1.0f;
		}
		while (saturationAge / (saturationAge + 1.0f) < feedback)
		{
			saturationAge += 1.0f;
		}
		return saturationAge;
	}

	[[nodiscard]] inline float ResolveTemporalAAHistoryAgePreview(
		float nextHistoryAge, float maxHistoryFeedback) noexcept
	{
		if (!IsTemporalHistoryAgeValid(nextHistoryAge))
		{
			return 0.0f;
		}

		const float feedbackSaturationAge =
			ResolveTemporalAAFeedbackSaturationAge(maxHistoryFeedback);
		// Feedback uses PreviousAge while this preview displays stored NextAge.
		// Keep the saturation-age denominator intact; there is intentionally no -1.
		return std::clamp((nextHistoryAge - TemporalHistoryInitialAge) /
			std::max(feedbackSaturationAge, TemporalHistoryInitialAge), 0.0f, 1.0f);
	}

	struct TemporalAASettings
	{
		bool m_Enabled = false;
		float m_DepthAbsoluteThreshold = TemporalAADepthAbsoluteThreshold;
		float m_DepthRelativeThreshold = TemporalAADepthRelativeThreshold;
		float m_MaxHistoryFeedback = TemporalAADefaultMaxHistoryFeedback;
		float m_VelocityWeightScale = TemporalAADefaultVelocityWeightScale;
		float m_LuminanceWeightScale = TemporalAADefaultLuminanceWeightScale;
		float m_NeighborhoodClampExpansion = TemporalAADefaultNeighborhoodClampExpansion;

		bool operator==(const TemporalAASettings&) const noexcept = default;
	};

	[[nodiscard]] inline TemporalAASettings ResolveTemporalAASettings(
		TemporalAASettings settings) noexcept
	{
		const TemporalAASettings defaults{};
		settings.m_DepthAbsoluteThreshold = std::isfinite(settings.m_DepthAbsoluteThreshold)
			? std::clamp(settings.m_DepthAbsoluteThreshold, 0.0f,
				TemporalAAMaxDepthThreshold)
			: defaults.m_DepthAbsoluteThreshold;
		settings.m_DepthRelativeThreshold = std::isfinite(settings.m_DepthRelativeThreshold)
			? std::clamp(settings.m_DepthRelativeThreshold, 0.0f,
				TemporalAAMaxDepthThreshold)
			: defaults.m_DepthRelativeThreshold;
		settings.m_MaxHistoryFeedback = std::isfinite(settings.m_MaxHistoryFeedback)
			? std::clamp(settings.m_MaxHistoryFeedback, 0.0f,
				TemporalAAMaxHistoryFeedbackCeiling)
			: defaults.m_MaxHistoryFeedback;
		settings.m_VelocityWeightScale = std::isfinite(settings.m_VelocityWeightScale)
			? std::clamp(settings.m_VelocityWeightScale, 0.0f,
				TemporalAAMaxVelocityWeightScale)
			: defaults.m_VelocityWeightScale;
		settings.m_LuminanceWeightScale = std::isfinite(settings.m_LuminanceWeightScale)
			? std::clamp(settings.m_LuminanceWeightScale, 0.0f,
				TemporalAAMaxLuminanceWeightScale)
			: defaults.m_LuminanceWeightScale;
		settings.m_NeighborhoodClampExpansion =
			std::isfinite(settings.m_NeighborhoodClampExpansion)
			? std::clamp(settings.m_NeighborhoodClampExpansion, 0.0f,
				TemporalAAMaxNeighborhoodClampExpansion)
			: defaults.m_NeighborhoodClampExpansion;
		return settings;
	}

	[[nodiscard]] inline float ResolveTemporalAAHistoryWeight(float previousHistoryAge,
		float motionMagnitudePixels, float currentLuminance, float historyLuminance,
		const TemporalAASettings& settings) noexcept
	{
		if (!IsTemporalHistoryAgeValid(previousHistoryAge) ||
			!std::isfinite(motionMagnitudePixels) || motionMagnitudePixels < 0.0f ||
			!std::isfinite(currentLuminance) || !std::isfinite(historyLuminance))
		{
			return 0.0f;
		}

		const TemporalAASettings resolved = ResolveTemporalAASettings(settings);
		const float ageWeight = previousHistoryAge / (previousHistoryAge + 1.0f);
		const float baseHistoryWeight =
			std::min(ageWeight, resolved.m_MaxHistoryFeedback);
		const float velocityConfidence = 1.0f - std::clamp(
			motionMagnitudePixels * resolved.m_VelocityWeightScale, 0.0f, 1.0f);
		const float luminanceDenominator =
			std::max({ std::abs(currentLuminance), std::abs(historyLuminance), 1.0e-4f });
		const float relativeLuminanceDifference =
			std::abs(currentLuminance - historyLuminance) / luminanceDenominator;
		const float luminanceConfidence = 1.0f - std::clamp(
			relativeLuminanceDifference * resolved.m_LuminanceWeightScale, 0.0f, 1.0f);
		return baseHistoryWeight * velocityConfidence * luminanceConfidence;
	}

	enum class TemporalAAHistoryRejectionReason : uint32_t
	{
		None,
		HistoryUnavailable,
		PreviousUVOutOfBounds,
		NonFinite,
		DepthMismatch,
		BackgroundMismatch,
	};

	[[nodiscard]] inline bool IsTemporalSkyHistoryCompatible(
		float previousRawDepth, DepthConvention previousDepthConvention) noexcept
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

		const float tolerance =
			std::max(absoluteThreshold, relativeThreshold * expectedPreviousViewZ);
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

	enum class SceneExtensionTemporalParticipation : uint8_t
	{
		TemporalIntegrated,
		PostTAA,
		TemporalUnsupported,
	};

	struct TemporalAACapabilityStatus
	{
		bool m_MotionRenderTarget = false;
		bool m_MotionShaderResource = false;
		bool m_ResolvedColorRenderTarget = false;
		bool m_ResolvedColorShaderResource = false;
		bool m_ResolvedColorTypedUavStore = false;
		bool m_HistoryColorShaderResource = false;
		bool m_HistoryColorTypedUavStore = false;
		bool m_HistoryDepthShaderResource = false;
		bool m_HistoryDepthTypedUavStore = false;
		bool m_VelocityProgramsAvailable = false;
		bool m_ResolveProgramAvailable = false;
		bool m_BindingLayoutAvailable = false;

		[[nodiscard]] constexpr bool IsCoreAvailable() const noexcept
		{
			return m_MotionRenderTarget && m_MotionShaderResource &&
				m_ResolvedColorRenderTarget && m_ResolvedColorShaderResource &&
				m_ResolvedColorTypedUavStore && m_HistoryColorShaderResource &&
				m_HistoryColorTypedUavStore && m_HistoryDepthShaderResource &&
				m_HistoryDepthTypedUavStore && m_VelocityProgramsAvailable &&
				m_ResolveProgramAvailable && m_BindingLayoutAvailable;
		}

		bool operator==(const TemporalAACapabilityStatus&) const noexcept = default;
	};

	enum class TemporalAAFrameStatus : uint8_t
	{
		Disabled,
		Unavailable,
		Active,
	};

	enum class TemporalAADisableReason : uint8_t
	{
		None,
		NotRequested,
		CoreCapabilityUnavailable,
		DisplayViewIneligible,
		DepthVelocityPathUnavailable,
		SceneExtensionUnsupported,
	};

	struct TemporalFramePlanResolveInfo
	{
		TemporalAASettings m_Settings{};
		TemporalAACapabilityStatus m_Capabilities{};
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		SceneExtensionTemporalParticipation m_SceneExtensionParticipation =
			SceneExtensionTemporalParticipation::TemporalUnsupported;
		uint64_t m_ResetIdentity = 0;
		uint64_t m_SessionIdentity = 0;
		bool m_DisplayViewEligible = false;
		bool m_DepthVelocityPathAvailable = false;
		bool m_InternalContractMode = false;
	};

	struct ResolvedTemporalFramePlan
	{
		TemporalAACapabilityStatus m_Capabilities{};
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		SceneExtensionTemporalParticipation m_SceneExtensionParticipation =
			SceneExtensionTemporalParticipation::TemporalUnsupported;
		TemporalAAFrameStatus m_Status = TemporalAAFrameStatus::Disabled;
		TemporalAADisableReason m_DisableReason = TemporalAADisableReason::NotRequested;
		uint64_t m_ResetIdentity = 0;
		uint64_t m_SessionIdentity = 0;
		bool m_Requested = false;
		bool m_CoreAvailable = false;
		bool m_Active = false;
		bool m_InternalContractMode = false;
		bool m_DisplayViewEligible = false;
		bool m_DepthVelocityPathAvailable = false;

		bool operator==(const ResolvedTemporalFramePlan&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsTemporalAADisplayViewEligible(
		RenderViewID viewId, uint32_t width, uint32_t height) noexcept
	{
		return width > 0 && height > 0 &&
			(viewId == RenderViewID::Main || IsDebugCameraRenderViewID(viewId));
	}

	[[nodiscard]] constexpr ResolvedTemporalFramePlan ResolveTemporalFramePlan(
		const TemporalFramePlanResolveInfo& info) noexcept
	{
		ResolvedTemporalFramePlan plan{
			.m_Capabilities = info.m_Capabilities,
			.m_DisplayViewId = info.m_DisplayViewId,
			.m_SceneExtensionParticipation = info.m_SceneExtensionParticipation,
			.m_ResetIdentity = info.m_ResetIdentity,
			.m_SessionIdentity = info.m_SessionIdentity,
			.m_Requested = info.m_Settings.m_Enabled,
			.m_CoreAvailable = info.m_Capabilities.IsCoreAvailable(),
			.m_InternalContractMode = info.m_InternalContractMode,
			.m_DisplayViewEligible = info.m_DisplayViewEligible,
			.m_DepthVelocityPathAvailable = info.m_DepthVelocityPathAvailable,
		};

		if (!plan.m_Requested)
		{
			return plan;
		}

		plan.m_Status = TemporalAAFrameStatus::Unavailable;
		if (!plan.m_CoreAvailable && !plan.m_InternalContractMode)
		{
			plan.m_DisableReason = TemporalAADisableReason::CoreCapabilityUnavailable;
			return plan;
		}
		if (!plan.m_DisplayViewEligible)
		{
			plan.m_DisableReason = TemporalAADisableReason::DisplayViewIneligible;
			return plan;
		}
		if (!plan.m_DepthVelocityPathAvailable)
		{
			plan.m_DisableReason = TemporalAADisableReason::DepthVelocityPathUnavailable;
			return plan;
		}
		if (plan.m_SceneExtensionParticipation ==
			SceneExtensionTemporalParticipation::TemporalUnsupported)
		{
			plan.m_DisableReason = TemporalAADisableReason::SceneExtensionUnsupported;
			return plan;
		}

		plan.m_Status = TemporalAAFrameStatus::Active;
		plan.m_DisableReason = TemporalAADisableReason::None;
		plan.m_Active = true;
		return plan;
	}

	namespace temporal
	{
		inline constexpr uint32_t JitterSampleCount = 8;
		inline constexpr std::array<Vector2, JitterSampleCount> JitterSamplesPixels = {
			Vector2(0.0f, -1.0f / 6.0f),
			Vector2(-1.0f / 4.0f, 1.0f / 6.0f),
			Vector2(1.0f / 4.0f, -7.0f / 18.0f),
			Vector2(-3.0f / 8.0f, -1.0f / 18.0f),
			Vector2(1.0f / 8.0f, 5.0f / 18.0f),
			Vector2(-1.0f / 8.0f, -5.0f / 18.0f),
			Vector2(3.0f / 8.0f, 1.0f / 18.0f),
			Vector2(-7.0f / 16.0f, 7.0f / 18.0f),
		};
		[[nodiscard]] constexpr Vector2 GetJitterSamplePixels(uint32_t sequenceIndex) noexcept
		{
			return JitterSamplesPixels[sequenceIndex % JitterSampleCount];
		}

		[[nodiscard]] inline Vector2 JitterPixelsToUV(
			const Vector2& jitterPixels, uint32_t width, uint32_t height) noexcept
		{
			const float safeWidth = static_cast<float>(std::max(width, 1u));
			const float safeHeight = static_cast<float>(std::max(height, 1u));
			return Vector2(jitterPixels.m_X / safeWidth, jitterPixels.m_Y / safeHeight);
		}

		[[nodiscard]] inline Vector2 JitterPixelsToNDC(
			const Vector2& jitterPixels, uint32_t width, uint32_t height) noexcept
		{
			const Vector2 jitterUV = JitterPixelsToUV(jitterPixels, width, height);
			return Vector2(2.0f * jitterUV.m_X, -2.0f * jitterUV.m_Y);
		}

		[[nodiscard]] inline Vector4 ApplyJitterToClipPosition(
			const Vector4& unjitteredClip, const Vector2& jitterNDC) noexcept
		{
			return Vector4(unjitteredClip.m_X + jitterNDC.m_X * unjitteredClip.m_W,
				unjitteredClip.m_Y + jitterNDC.m_Y * unjitteredClip.m_W,
				unjitteredClip.m_Z, unjitteredClip.m_W);
		}

		[[nodiscard]] inline Vector2 ReprojectToPreviousUV(
			const Vector2& currentUV, const Vector2& motionUV) noexcept
		{
			return currentUV - motionUV;
		}
	}
}
