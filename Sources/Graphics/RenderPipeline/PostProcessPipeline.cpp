#include "Core/Precompiled.h"
#include "Graphics/RenderPipeline/PostProcessPipeline.h"
#include "Graphics/PostProcess/PostProcessGraphResources.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"

namespace gglab
{
	void PostProcessPipeline::AddPasses(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		const RenderViewID displayViewId = context.GetDisplayViewId();
		auto& targets = rg.GetBlackboard()
			.Get<RGViewTargetsTable>(ViewTargetsTableName)
			.GetViewTargets(displayViewId);

		auto& resources = rg.GetBlackboard()
			.GetOrCreate<RGPostProcessResources>(PostProcessResourcesName);
		resources = {
			.m_Inputs = {
				.m_SceneColor = {
					.m_Texture = targets.m_SceneColor,
					.m_State = PostProcessColorState::SceneLinearRec709,
					.m_PreExposure = 1.0f,
				},
			},
			.m_Output = {
				.m_Texture = targets.m_BackBuffer,
				.m_Transform = {
					.m_Mode = OutputColorMode::SdrSRGB,
				},
			},
		};

		m_BloomPass.AddPass(rg, context, services);
		m_FinalColorPass.AddPass(rg, context, services);
		// Publish the version written by FinalColor to downstream presentation passes.
		targets.m_BackBuffer = resources.m_Output.m_Texture;
	}
}
