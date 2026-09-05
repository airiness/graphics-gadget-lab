#pragma once
#include "Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"

#include <string_view>

namespace gglab
{
	struct RenderView;
	struct RenderScene;
	class Renderer;
	class RenderGraph;

	class RenderPipelineBase
	{
	public:
		RenderPipelineBase() noexcept = default;
		virtual ~RenderPipelineBase() = default;

		virtual std::string_view GetName() const noexcept = 0;
		virtual void PrepareTemporalFramePlanning(const RenderServices&) noexcept {}

		virtual ResolvedTemporalFramePlan ResolveTemporalFramePlan(
			TemporalFramePlanResolveInfo info) const noexcept
		{
			info.m_DepthVelocityPathAvailable = false;
			info.m_SceneExtensionParticipation =
				SceneExtensionTemporalParticipation::TemporalUnsupported;
			return gglab::ResolveTemporalFramePlan(info);
		}

		virtual void BuildRenderGraph(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept = 0;
		[[nodiscard]] virtual bool ValidateRenderFrame(
			const RenderFrameContext&, const RenderServices&) noexcept
		{
			return true;
		}
	};
}
