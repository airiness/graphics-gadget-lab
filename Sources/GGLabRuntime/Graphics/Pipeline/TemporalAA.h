#pragma once

#include "Core/Math/Vector.h"
#include "Graphics/GraphicsTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gglab
{
	struct TemporalAASettings
	{
		bool m_Enabled = false;

		bool operator==(const TemporalAASettings&) const noexcept = default;
	};

	inline constexpr float TemporalAADepthAbsoluteThreshold = 0.05f;
	inline constexpr float TemporalAADepthRelativeThreshold = 0.02f;
	inline constexpr float TemporalAARestrictedHistoryWeight = 0.125f;

	enum class TemporalAAHistoryRejectionReason : uint32_t
	{
		None,
		HistoryUnavailable,
		PreviousUVOutOfBounds,
		NonFinite,
		DepthMismatch,
	};

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
