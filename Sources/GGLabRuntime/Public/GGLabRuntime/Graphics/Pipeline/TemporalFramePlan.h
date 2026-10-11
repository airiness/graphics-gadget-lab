#pragma once

#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gglab
{
	// Temporal consumers of the display view. Each consumer resolves its own eligibility;
	// only an active consumer contributes the services it requires to the frame plan.
	enum class TemporalConsumer : uint8_t
	{
		// Temporal AA resolve of the display color.
		TemporalAA,
		// Supersampled reference accumulation of an evaluation sequence frame.
		Reference,
		// Temporal GTAO: a sampling sequence advanced per submitted frame, accumulated in
		// its own visibility history when no resolve integrates the display color.
		AmbientOcclusion,
		Count,
	};
	inline constexpr uint32_t TemporalConsumerCount =
		static_cast<uint32_t>(TemporalConsumer::Count);

	[[nodiscard]] constexpr std::string_view GetTemporalConsumerName(
		TemporalConsumer consumer) noexcept
	{
		switch (consumer)
		{
		case TemporalConsumer::TemporalAA: return "temporal-aa";
		case TemporalConsumer::Reference: return "reference";
		case TemporalConsumer::AmbientOcclusion: return "temporal-gtao";
		case TemporalConsumer::Count: break;
		}
		return "unknown";
	}

	// Per-frame temporal services. A frame enables exactly the union of the services that
	// its active consumers require, all together or not at all.
	enum class TemporalService : uint8_t
	{
		None = 0u,
		// Sub-pixel jitter of the display view's raster projection. Only a consumer that
		// owns a resolve removing the jitter requires it.
		ProjectionJitter = 1u << 0,
		// Raster motion vectors written with the depth prepass.
		GeometryMotion = 1u << 1,
		// Submitted-frame continuity: previous view and object state and the frame index,
		// committed only after a successful submission.
		FrameContinuity = 1u << 2,
		// Persistent display color and depth history of the Temporal AA resolve.
		ColorDepthHistory = 1u << 3,
	};
	GGLAB_ENUM_FLAGS(TemporalService);
	inline constexpr std::array<TemporalService, 4> TemporalServices{
		TemporalService::ProjectionJitter,
		TemporalService::GeometryMotion,
		TemporalService::FrameContinuity,
		TemporalService::ColorDepthHistory,
	};

	[[nodiscard]] constexpr std::string_view GetTemporalServiceName(
		TemporalService service) noexcept
	{
		switch (service)
		{
		case TemporalService::ProjectionJitter: return "projection-jitter";
		case TemporalService::GeometryMotion: return "geometry-motion";
		case TemporalService::FrameContinuity: return "frame-continuity";
		case TemporalService::ColorDepthHistory: return "color-depth-history";
		default: break;
		}
		return "unknown";
	}

	[[nodiscard]] constexpr TemporalService GetTemporalConsumerRequiredServices(
		TemporalConsumer consumer) noexcept
	{
		switch (consumer)
		{
		case TemporalConsumer::TemporalAA:
			return TemporalService::ProjectionJitter | TemporalService::GeometryMotion |
				TemporalService::FrameContinuity | TemporalService::ColorDepthHistory;
		case TemporalConsumer::Reference:
			// The reference owns a separate jitter sequence and removes it by averaging;
			// time is held, so it needs no motion, continuity or history.
			return TemporalService::ProjectionJitter;
		case TemporalConsumer::AmbientOcclusion:
			// GTAO history follows the raster samples, jittered or not, and varies its
			// sampling with the submitted frame index; it removes no jitter itself.
			return TemporalService::GeometryMotion | TemporalService::FrameContinuity;
		case TemporalConsumer::Count: break;
		}
		return TemporalService::None;
	}

	enum class TemporalConsumerStatus : uint8_t
	{
		Disabled,
		Unavailable,
		Active,
	};

	// Why a consumer contributes no services. Unavailable reasons are expected states of
	// the device, view or pipeline, not contract failures.
	enum class TemporalConsumerDisableReason : uint8_t
	{
		None,
		NotRequested,
		CoreCapabilityUnavailable,
		DisplayViewIneligible,
		DepthVelocityPathUnavailable,
		SceneExtensionUnsupported,
	};

	[[nodiscard]] constexpr std::string_view GetTemporalConsumerStatusName(
		TemporalConsumerStatus status) noexcept
	{
		switch (status)
		{
		case TemporalConsumerStatus::Disabled: return "disabled";
		case TemporalConsumerStatus::Unavailable: return "unavailable";
		case TemporalConsumerStatus::Active: return "active";
		}
		return "unknown";
	}

	[[nodiscard]] constexpr std::string_view GetTemporalConsumerDisableReasonName(
		TemporalConsumerDisableReason reason) noexcept
	{
		switch (reason)
		{
		case TemporalConsumerDisableReason::None: return "none";
		case TemporalConsumerDisableReason::NotRequested: return "not-requested";
		case TemporalConsumerDisableReason::CoreCapabilityUnavailable:
			return "core-capability-unavailable";
		case TemporalConsumerDisableReason::DisplayViewIneligible:
			return "display-view-ineligible";
		case TemporalConsumerDisableReason::DepthVelocityPathUnavailable:
			return "depth-velocity-path-unavailable";
		case TemporalConsumerDisableReason::SceneExtensionUnsupported:
			return "scene-extension-unsupported";
		}
		return "unknown";
	}

	struct TemporalConsumerPlan
	{
		TemporalConsumerStatus m_Status = TemporalConsumerStatus::Disabled;
		TemporalConsumerDisableReason m_DisableReason =
			TemporalConsumerDisableReason::NotRequested;
		// The consumer's required services while it is active; none otherwise.
		TemporalService m_Services = TemporalService::None;
		bool m_Requested = false;

		[[nodiscard]] constexpr bool IsActive() const noexcept
		{
			return m_Status == TemporalConsumerStatus::Active;
		}

		bool operator==(const TemporalConsumerPlan&) const noexcept = default;
	};

	// Inputs of the Temporal AA consumer.
	struct TemporalAAPlanResolveInfo
	{
		TemporalAASettings m_Settings{};
		TemporalAACapabilityStatus m_Capabilities{};
		// The pipeline's resolve reconstructs the display extent from a smaller render
		// extent; without it every frame renders at native resolution.
		bool m_UpscalingAvailable = false;
	};

	// Outputs of the Temporal AA consumer. The capabilities describe the device whether or
	// not the consumer is active; the effective settings keep their defaults while it is
	// inactive.
	struct ResolvedTemporalAAPlan
	{
		TemporalAACapabilityStatus m_Capabilities{};
		// Effective render resolution of the display view: the requested preset while the
		// Temporal AA consumer is active and the resolve can upscale, native otherwise.
		TemporalAAResolutionPreset m_ResolutionPreset = TemporalAAResolutionPreset::Native;
		// Accumulation model of the Temporal AA color history while that consumer is active.
		TemporalAAHistoryAccumulation m_HistoryAccumulation =
			TemporalAAHistoryAccumulation::EffectiveSamples;
		// Material texture LOD offset of the Temporal AA consumer while it is active, added
		// to log2(render / display); zero otherwise.
		float m_TextureLodBiasOffset = 0.0f;
		bool m_CoreAvailable = false;

		// Phases of the Temporal AA jitter sequence at the effective resolution preset.
		[[nodiscard]] constexpr uint32_t GetJitterSequenceLength() const noexcept
		{
			return GetTemporalAAJitterSequenceLength(m_ResolutionPreset);
		}

		bool operator==(const ResolvedTemporalAAPlan&) const noexcept = default;
	};

	struct TemporalFramePlanResolveInfo
	{
		TemporalAAPlanResolveInfo m_TemporalAA{};
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		SceneExtensionTemporalParticipation m_SceneExtensionParticipation =
			SceneExtensionTemporalParticipation::TemporalUnsupported;
		uint64_t m_ResetIdentity = 0;
		uint64_t m_SessionIdentity = 0;
		bool m_DisplayViewEligible = false;
		bool m_DepthVelocityPathAvailable = false;
		// A supersampled reference sample is due this frame. It excludes Temporal AA and
		// temporal GTAO.
		bool m_ReferenceRequested = false;
		// Temporal GTAO accumulation is requested for the display view.
		bool m_AmbientOcclusionRequested = false;
		// The pipeline evaluates GTAO and the device supports its persistent history.
		bool m_AmbientOcclusionAvailable = false;
	};

	// The display view's temporal plan, resolved once per frame from its consumers.
	struct ResolvedTemporalFramePlan
	{
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		SceneExtensionTemporalParticipation m_SceneExtensionParticipation =
			SceneExtensionTemporalParticipation::TemporalUnsupported;
		std::array<TemporalConsumerPlan, TemporalConsumerCount> m_Consumers{};
		// Union of the active consumers' services.
		TemporalService m_Services = TemporalService::None;
		ResolvedTemporalAAPlan m_TemporalAA{};
		uint64_t m_ResetIdentity = 0;
		uint64_t m_SessionIdentity = 0;
		bool m_DisplayViewEligible = false;
		bool m_DepthVelocityPathAvailable = false;

		[[nodiscard]] constexpr const TemporalConsumerPlan& GetConsumer(
			TemporalConsumer consumer) const noexcept
		{
			return m_Consumers[static_cast<uint32_t>(consumer)];
		}

		[[nodiscard]] constexpr bool IsConsumerActive(TemporalConsumer consumer) const noexcept
		{
			return GetConsumer(consumer).IsActive();
		}

		[[nodiscard]] constexpr bool HasService(TemporalService service) const noexcept
		{
			return Test(m_Services, service);
		}

		// Temporal GTAO keeps its own visibility history only while Temporal AA is inactive.
		// The resolve integrates the display color that the visibility shades, so a second
		// history would filter the same samples twice and drift with each jittered
		// reprojection; under the resolve, temporal GTAO only advances its sampling sequence.
		[[nodiscard]] constexpr bool UsesAmbientOcclusionHistory() const noexcept
		{
			return IsConsumerActive(TemporalConsumer::AmbientOcclusion) &&
				!IsConsumerActive(TemporalConsumer::TemporalAA);
		}

		// The active consumer whose resolve removes the projection jitter, if any.
		[[nodiscard]] constexpr std::optional<TemporalConsumer> GetProjectionJitterOwner()
			const noexcept
		{
			for (uint32_t index = 0; index < TemporalConsumerCount; ++index)
			{
				if (Test(m_Consumers[index].m_Services, TemporalService::ProjectionJitter))
				{
					return static_cast<TemporalConsumer>(index);
				}
			}
			return std::nullopt;
		}

		bool operator==(const ResolvedTemporalFramePlan&) const noexcept = default;
	};

	[[nodiscard]] constexpr TemporalConsumerPlan ResolveTemporalAAConsumerPlan(
		const TemporalFramePlanResolveInfo& info) noexcept
	{
		TemporalConsumerPlan consumer{ .m_Requested = info.m_TemporalAA.m_Settings.m_Enabled };
		if (!consumer.m_Requested)
		{
			return consumer;
		}

		consumer.m_Status = TemporalConsumerStatus::Unavailable;
		if (!info.m_TemporalAA.m_Capabilities.IsCoreAvailable())
		{
			consumer.m_DisableReason = TemporalConsumerDisableReason::CoreCapabilityUnavailable;
			return consumer;
		}
		if (!info.m_DisplayViewEligible)
		{
			consumer.m_DisableReason = TemporalConsumerDisableReason::DisplayViewIneligible;
			return consumer;
		}
		if (!info.m_DepthVelocityPathAvailable)
		{
			consumer.m_DisableReason = TemporalConsumerDisableReason::DepthVelocityPathUnavailable;
			return consumer;
		}
		if (info.m_SceneExtensionParticipation ==
			SceneExtensionTemporalParticipation::TemporalUnsupported)
		{
			consumer.m_DisableReason = TemporalConsumerDisableReason::SceneExtensionUnsupported;
			return consumer;
		}

		consumer.m_Status = TemporalConsumerStatus::Active;
		consumer.m_DisableReason = TemporalConsumerDisableReason::None;
		consumer.m_Services = GetTemporalConsumerRequiredServices(TemporalConsumer::TemporalAA);
		return consumer;
	}

	[[nodiscard]] constexpr TemporalConsumerPlan ResolveTemporalAmbientOcclusionConsumerPlan(
		const TemporalFramePlanResolveInfo& info) noexcept
	{
		TemporalConsumerPlan consumer{ .m_Requested = info.m_AmbientOcclusionRequested };
		if (!consumer.m_Requested)
		{
			return consumer;
		}

		consumer.m_Status = TemporalConsumerStatus::Unavailable;
		if (!info.m_AmbientOcclusionAvailable)
		{
			consumer.m_DisableReason = TemporalConsumerDisableReason::CoreCapabilityUnavailable;
			return consumer;
		}
		if (!info.m_DisplayViewEligible)
		{
			consumer.m_DisableReason = TemporalConsumerDisableReason::DisplayViewIneligible;
			return consumer;
		}
		if (!info.m_DepthVelocityPathAvailable)
		{
			consumer.m_DisableReason = TemporalConsumerDisableReason::DepthVelocityPathUnavailable;
			return consumer;
		}

		consumer.m_Status = TemporalConsumerStatus::Active;
		consumer.m_DisableReason = TemporalConsumerDisableReason::None;
		consumer.m_Services =
			GetTemporalConsumerRequiredServices(TemporalConsumer::AmbientOcclusion);
		return consumer;
	}

	// The reference has no eligibility gate: a requested sample that cannot be
	// accumulated is a frame contract failure of the pipeline.
	[[nodiscard]] constexpr TemporalConsumerPlan ResolveTemporalReferenceConsumerPlan(
		const TemporalFramePlanResolveInfo& info) noexcept
	{
		if (!info.m_ReferenceRequested)
		{
			return {};
		}
		return {
			.m_Status = TemporalConsumerStatus::Active,
			.m_DisableReason = TemporalConsumerDisableReason::None,
			.m_Services = GetTemporalConsumerRequiredServices(TemporalConsumer::Reference),
			.m_Requested = true,
		};
	}

	[[nodiscard]] inline ResolvedTemporalFramePlan ResolveTemporalFramePlan(
		const TemporalFramePlanResolveInfo& info) noexcept
	{
		// Both consumers own a jitter sequence; a reference frame holds Temporal AA off.
		GGLAB_ASSERT_MSG(!info.m_ReferenceRequested || !info.m_TemporalAA.m_Settings.m_Enabled,
			"A temporal reference sample requires Temporal AA to be unrequested.");
		// The reference holds time and averages GTAO over its own sample sequence.
		GGLAB_ASSERT_MSG(!info.m_ReferenceRequested || !info.m_AmbientOcclusionRequested,
			"A temporal reference sample requires temporal GTAO to be unrequested.");
		ResolvedTemporalFramePlan plan{
			.m_DisplayViewId = info.m_DisplayViewId,
			.m_SceneExtensionParticipation = info.m_SceneExtensionParticipation,
			.m_TemporalAA = {
				.m_Capabilities = info.m_TemporalAA.m_Capabilities,
				.m_CoreAvailable = info.m_TemporalAA.m_Capabilities.IsCoreAvailable(),
			},
			.m_ResetIdentity = info.m_ResetIdentity,
			.m_SessionIdentity = info.m_SessionIdentity,
			.m_DisplayViewEligible = info.m_DisplayViewEligible,
			.m_DepthVelocityPathAvailable = info.m_DepthVelocityPathAvailable,
		};
		plan.m_Consumers[static_cast<uint32_t>(TemporalConsumer::TemporalAA)] =
			ResolveTemporalAAConsumerPlan(info);
		plan.m_Consumers[static_cast<uint32_t>(TemporalConsumer::Reference)] =
			ResolveTemporalReferenceConsumerPlan(info);
		plan.m_Consumers[static_cast<uint32_t>(TemporalConsumer::AmbientOcclusion)] =
			ResolveTemporalAmbientOcclusionConsumerPlan(info);
		uint32_t jitterOwnerCount = 0;
		for (const TemporalConsumerPlan& consumer : plan.m_Consumers)
		{
			plan.m_Services |= consumer.m_Services;
			jitterOwnerCount +=
				Test(consumer.m_Services, TemporalService::ProjectionJitter) ? 1u : 0u;
		}
		GGLAB_ASSERT_MSG(jitterOwnerCount <= 1,
			"At most one active temporal consumer owns the projection jitter.");
		if (plan.IsConsumerActive(TemporalConsumer::TemporalAA))
		{
			const TemporalAASettings& settings = info.m_TemporalAA.m_Settings;
			plan.m_TemporalAA.m_HistoryAccumulation = settings.m_HistoryAccumulation;
			plan.m_TemporalAA.m_TextureLodBiasOffset = settings.m_TextureLodBiasOffset;
			if (info.m_TemporalAA.m_UpscalingAvailable)
			{
				plan.m_TemporalAA.m_ResolutionPreset = settings.m_ResolutionPreset;
			}
		}
		return plan;
	}
}
