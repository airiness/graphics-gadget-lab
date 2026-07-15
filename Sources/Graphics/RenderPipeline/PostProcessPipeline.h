#pragma once
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
		RenderPassFinalColor m_FinalColorPass;
	};
}
