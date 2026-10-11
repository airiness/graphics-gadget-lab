#pragma once

#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessColorState.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <vector>

namespace gglab
{
	// Persistent history RGB stores accumulated color; alpha stores the accumulation
	// state of the history's accumulation model.
	inline constexpr RHIFormat TemporalHistoryColorFormat = RHIFormat::R16G16B16A16Float;
	inline constexpr RHIFormat TemporalHistoryDepthFormat = RHIFormat::R32Float;
	// Per-pixel reliability evidence that travels with the color history across frames:
	// the smoothed signed (R) and absolute (G) relative luminance difference of the current
	// frame from accepted history.
	inline constexpr RHIFormat TemporalHistoryReliabilityFormat = RHIFormat::R16G16Float;

	enum class TemporalHistoryResetReason : uint8_t
	{
		None,
		ColdStart,
		Disabled,
		DisplayViewChanged,
		ResetIdentityChanged,
		SessionIdentityChanged,
		ExtentChanged,
		FormatChanged,
		ColorAbiChanged,
		AccumulationChanged,
		AllocationFailure,
		AvailabilityChanged,
		ResolveProgramChanged,
		FatalSubmission,
		Resume,
		Shutdown,
		InvalidExposureMetadata,
	};

	struct TemporalHistoryCompatibilityIdentity
	{
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		uint64_t m_ResetIdentity = 0;
		uint64_t m_SessionIdentity = 0;
		// The resolved color history stores display pixels; the depth history that
		// validates reprojection stores the render-domain samples it was rasterized at.
		ViewExtent m_ColorExtent{};
		ViewExtent m_DepthExtent{};
		RHIFormat m_ColorFormat = TemporalHistoryColorFormat;
		RHIFormat m_DepthFormat = TemporalHistoryDepthFormat;
		RHIFormat m_ReliabilityFormat = TemporalHistoryReliabilityFormat;
		TemporalColorAbi m_ColorAbi = ActiveTemporalColorAbi;
		// Meaning of the stored alpha.
		TemporalAAHistoryAccumulation m_Accumulation =
			TemporalAAHistoryAccumulation::EffectiveSamples;

		bool operator==(const TemporalHistoryCompatibilityIdentity&) const noexcept = default;
	};

	struct TemporalHistoryCommittedMetadata
	{
		TemporalHistoryCompatibilityIdentity m_Compatibility{};
		Vector2 m_JitterUV = Vector2::Zero;
		uint32_t m_JitterIndex = 0;
		float m_PreExposure = SceneColorStoragePreExposureV1;
		RHIFencePoint m_GraphicsFence{};
	};

	struct TemporalHistoryFrameState
	{
		uint64_t m_AllocationGeneration = 0;
		uint32_t m_ReadIndex = 0;
		uint32_t m_WriteIndex = 1;
		// Latched from the successfully committed read generation, never the pending write.
		float m_PreviousPreExposure = 1.0f;
		bool m_Active = false;
		bool m_PreviousValid = false;
		bool m_RenderGraphImported = false;
		bool m_RenderGraphExported = false;
		bool m_Ended = false;
	};

	struct TemporalHistoryRenderGraphResources
	{
		RGTextureId m_PreviousColor;
		RGTextureId m_PreviousDepth;
		RGTextureId m_NextColor;
		RGTextureId m_NextDepth;
		// Display extent, like the color history.
		RGTextureId m_PreviousReliability;
		RGTextureId m_NextReliability;
		uint32_t m_ReadIndex = 0;
		uint32_t m_WriteIndex = 1;
		bool m_PreviousValid = false;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_PreviousColor.IsValid() && m_PreviousDepth.IsValid() &&
				m_NextColor.IsValid() && m_NextDepth.IsValid() &&
				m_PreviousReliability.IsValid() && m_NextReliability.IsValid();
		}
	};

	// Constant-size observation without texture-pool scans or retirement vectors.
	struct TemporalHistorySummary
	{
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		uint64_t m_SessionIdentity = 0;
		TemporalHistoryResetReason m_LastResetReason = TemporalHistoryResetReason::None;
		bool m_HasActiveHistory = false;
		bool m_HistoryValid = false;
	};

	struct TemporalHistoryManagerDiagnostics
	{
		TemporalHistoryCompatibilityIdentity m_Compatibility{};
		TemporalHistoryCommittedMetadata m_LastCommitted{};
		TemporalHistoryResetReason m_LastResetReason = TemporalHistoryResetReason::None;
		uint64_t m_AllocationGeneration = 0;
		uint64_t m_ResetCount = 0;
		uint64_t m_ActiveBytes = 0;
		uint64_t m_PendingRetirementBytes = 0;
		uint32_t m_ReadIndex = 0;
		bool m_HasActiveHistory = false;
		bool m_HistoryValid = false;
		std::vector<RHIFencePoint> m_PendingRetirementFences;
	};
}
