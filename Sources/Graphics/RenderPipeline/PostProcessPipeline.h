#pragma once
#include "Graphics/RenderPass/RenderPassBloom.h"
#include "Graphics/RenderPass/RenderPassFinalColor.h"

namespace gglab
{
	class PostProcessPipeline
	{
	public:
		void AddPasses(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept;

	private:
		RenderPassBloom m_BloomPass;
		RenderPassFinalColor m_FinalColorPass;
	};
}
