#pragma once

#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <vector>

namespace gglab
{
	// Persistent history RGB stores accumulated color; alpha stores HistoryAge.
	inline constexpr RHIFormat TemporalHistoryColorFormat = RHIFormat::R16G16B16A16Float;
	inline constexpr RHIFormat TemporalHistoryDepthFormat = RHIFormat::R32Float;

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
		AllocationFailure,
		AvailabilityChanged,
		ResolveProgramChanged,
		FatalSubmission,
		Resume,
		Shutdown,
	};

	struct TemporalHistoryCompatibilityIdentity
	{
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		uint64_t m_ResetIdentity = 0;
		uint64_t m_SessionIdentity = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_ColorFormat = TemporalHistoryColorFormat;
		RHIFormat m_DepthFormat = TemporalHistoryDepthFormat;

		bool operator==(const TemporalHistoryCompatibilityIdentity&) const noexcept = default;
	};

	struct TemporalHistoryCommittedMetadata
	{
		TemporalHistoryCompatibilityIdentity m_Compatibility{};
		Vector2 m_JitterUV = Vector2::Zero;
		uint32_t m_JitterIndex = 0;
		RHIFencePoint m_GraphicsFence{};
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
