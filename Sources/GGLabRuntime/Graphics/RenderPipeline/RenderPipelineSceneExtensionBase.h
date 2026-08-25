#pragma once

#include "Graphics/Pipeline/TemporalAA.h"

namespace gglab
{
	class RenderGraph;
	struct RenderFrameContext;
	struct RenderServices;

	class RenderPipelineSceneExtensionBase
	{
	public:
		RenderPipelineSceneExtensionBase() noexcept = default;
		RenderPipelineSceneExtensionBase(const RenderPipelineSceneExtensionBase&) = delete;
		RenderPipelineSceneExtensionBase& operator=(const RenderPipelineSceneExtensionBase&) = delete;
		RenderPipelineSceneExtensionBase(RenderPipelineSceneExtensionBase&&) = delete;
		RenderPipelineSceneExtensionBase& operator=(RenderPipelineSceneExtensionBase&&) = delete;
		virtual ~RenderPipelineSceneExtensionBase() = default;

		virtual SceneExtensionTemporalParticipation GetTemporalParticipation() const noexcept
		{
			return SceneExtensionTemporalParticipation::PostTAA;
		}

		virtual void AddOpaqueScenePasses(RenderGraph& renderGraph,
			const RenderFrameContext& frameContext, const RenderServices& services) noexcept = 0;
	};
}
