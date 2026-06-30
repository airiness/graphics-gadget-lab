#pragma once
#include "Graphics/RenderGraph/RGExecutionPlan.h"

namespace gglab
{
	class RGBarrierPlanner final
	{
	public:
		RGBarrierPlanner(
			std::vector<RGCompiledPass>& passes,
			std::vector<RGCompiledResource>& resources,
			const std::vector<RGPassNodeIndex>& executionOrder) noexcept;

		void Build() noexcept;

	private:
		std::vector<RGCompiledPass>& m_Passes;
		std::vector<RGCompiledResource>& m_Resources;
		const std::vector<RGPassNodeIndex>& m_ExecutionOrder;
	};
}
