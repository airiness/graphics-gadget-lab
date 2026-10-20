#pragma once
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"

#include <cstdint>
#include <string_view>

namespace gglab
{
	// Frame facts that decide whether the Forward+ recipe can record this frame.
	struct ForwardPlusFrameValidationInputs
	{
		// A non-ready readback verdict precedes every ordinary recipe fact below.
		RenderFrameValidationResult m_ReadbackValidation;
		bool m_PresentationAvailable = false;
		bool m_DisplayExtentMatchesPresentation = false;
		bool m_RenderSceneReady = false;
		bool m_MaterialDiagnostics = false;
		bool m_NativeResolution = true;
		// Opaque or alpha-test draws record Forward+ cull, opaque shading and GTAO.
		bool m_HasOpaqueDraws = false;
		uint32_t m_GlobalLightCount = 0;
		bool m_DepthCoverageValid = false;
		std::string_view m_DepthCoverageDiagnostic;
		bool m_GTAOEnabledAndSupported = false;
		bool m_GTAOPipelineAvailable = false;
		// The temporal plan accumulates GTAO in its own history this frame.
		bool m_GTAOHistoryRequired = false;
		bool m_GTAOTemporalPipelineAvailable = false;
		bool m_GTAOHistoryAvailable = false;
		bool m_TemporalActive = false;
		bool m_TemporalResolveClosureValid = false;
	};

	// The recipe always shades opaque draws with Forward+ after an EQUAL depth prepass.
	// A display view built for another swap-chain extent is an expected resize state and
	// skips the frame. Diagnostic MRTs require native render/display extents. Missing
	// scene data, invalid depth coverage and a lost temporal resolve
	// closure are contract failures. Frames that record opaque work additionally fail when
	// global lights exceed the Forward+ capacity, or when an enabled, supported GTAO has
	// unprepared recipes or lacks the accumulation recipe or history that its temporal plan
	// requires; transparent-only frames record neither and are not limited by them. No failure
	// selects another lighting path or frame topology or silently drops a requested feature.
	[[nodiscard]] RenderFrameValidationResult ClassifyForwardPlusFrame(
		const ForwardPlusFrameValidationInputs& inputs) noexcept;
}
