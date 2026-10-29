#pragma once
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneConstantBindings.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"

#include <array>

namespace gglab
{
	class RenderGraph;

	class RenderPassGIReceiver final
	{
	public:
		[[nodiscard]] RenderFrameValidationResult Prepare(const RenderFrameContext& frame,
			const RenderServices& services, ShaderID coverageVertex) noexcept;
		void AddPass(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services) noexcept;

	private:
		ResolvedRenderLayout m_Layout;
		ResolvedBindingRef<SceneGPU> m_Scene;
		GraphicsPhysicalPipelineKey m_Recipe;
		std::array<GraphicsPipelineSlot, 2> m_Slots{};
		std::array<ResolvedRenderPipeline, 2> m_Pipelines{};
	};
}
