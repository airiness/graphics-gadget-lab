#pragma once
#include "Graphics/RenderGraph/RGExecutionPlan.h"
#include "Graphics/Resource/TransientResourcePool.h"

namespace gglab
{
	class RHIDevice;
	struct RGExecuteContext;

	struct RGExecutorRuntime
	{
		RHIDevice* m_Device = nullptr;
		TransientResourcePool* m_TransientResourcePool = nullptr;
		std::vector<TransientTextureAllocation>* m_RetireTextures = nullptr;
		std::vector<TransientBufferAllocation>* m_RetireBuffers = nullptr;
	};

	class RGExecutor final
	{
	public:
		static void Execute(
			const RGExecutionPlan& plan,
			const RGExecutorRuntime& runtime,
			RGExecuteContext& executeContext) noexcept;
	};
}
