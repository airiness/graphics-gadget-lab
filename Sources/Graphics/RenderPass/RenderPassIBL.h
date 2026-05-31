#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/RenderPassIBLBrdfLUT.h"
#include "Graphics/RenderPass/RenderPassIBLEnvironment.h"
#include "Graphics/RenderPass/RenderPassIBLIrradiance.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderResourceRegistry.h"

namespace gglab
{
	class RenderPassIBL
	{
	public:
		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept;

	private:
		static RGTextureId ImportRuntimeTexture(RenderGraph::RGBuilder& builder,
			RenderResourceRegistry& registry,
			RenderResourceRegistry::TextureIndex texIndex,
			const char* name) noexcept;

	private:
		RenderPassIBLEnvironment m_IBLEnvironmentPass;
		RenderPassIBLIrradiance m_IBLIrradiancePass;
		RenderPassIBLBrdfLUT m_IBLBrdfLUTPass;
	};
}
