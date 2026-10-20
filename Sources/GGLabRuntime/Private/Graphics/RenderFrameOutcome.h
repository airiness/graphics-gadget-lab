#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"

#include <cstdint>

namespace gglab
{
	enum class RenderFrameOutcomeKind : uint8_t
	{
		Submitted,
		Discarded,
		Failed,
		CompletionUnknown,
	};

	struct RenderFrameOutcome
	{
		RenderFrameOutcomeKind m_Kind = RenderFrameOutcomeKind::Discarded;
		uint64_t m_FrameSerial = 0;
		// Submission fence on success, retention fence on discard/failure, invalid when
		// completion is unknown. An absent fence proves only that no work is outstanding.
		RHIFencePoint m_Fence{};
	};

	enum class RenderFrameEndPath : uint8_t
	{
		EndFrame,
		AbortActiveFrame,
	};

	// This is a settlement policy, not GPU execution history. In particular, EndFrame
	// Fatal() can follow work that executed and went idle; it must never become Discarded.
	// An abort without an RHI frame uses the default no-outstanding-work result.
	[[nodiscard]] inline RenderFrameOutcome ResolveRenderFrameOutcome(RenderFrameEndPath path,
		uint64_t frameSerial, const RHIFrameEndResult& result = RHIFrameEndResult::Fatal()) noexcept
	{
		if (result.IsCompletionUnknown())
		{
			return { RenderFrameOutcomeKind::CompletionUnknown, frameSerial, {} };
		}
		const RenderFrameOutcomeKind kind = path == RenderFrameEndPath::AbortActiveFrame ?
			RenderFrameOutcomeKind::Discarded :
			result.IsCompleted() ? RenderFrameOutcomeKind::Submitted : RenderFrameOutcomeKind::Failed;
		return { kind, frameSerial, result.GetSubmittedFence() };
	}
}
