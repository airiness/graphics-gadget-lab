#pragma once
#include "Graphics/RenderGraph/RenderGraph.h"

namespace gglab
{
	class RGBarrierCompiler final
	{
	public:
		RGBarrierCompiler(
			std::vector<RGVirtualResourceBase*>& virtualResources,
			std::vector<RGResourceNode>& resourceNodes,
			std::vector<RGPassNode>& passNodes,
			const std::vector<RGPassNodeIndex>& executionOrder) noexcept;

		void Build() noexcept;

	private:
		std::vector<RGVirtualResourceBase*>& m_VirtualResources;
		std::vector<RGResourceNode>& m_ResourceNodes;
		std::vector<RGPassNode>& m_PassNodes;
		const std::vector<RGPassNodeIndex>& m_ExecutionOrder;
	};
}
