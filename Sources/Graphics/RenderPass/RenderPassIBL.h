#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/RenderPassIBLBrdfLUT.h"
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
		RenderPassIBLBrdfLUT m_IBLBrdfLUTPass;
	};
}
