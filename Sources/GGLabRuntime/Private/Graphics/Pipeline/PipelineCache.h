#pragma once
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassInfo.h"
#include "GGLabRuntime/Graphics/RenderServices.h"

#include <cstdint>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace gglab
{
	class RHIPipelineSystem;
	class ShaderManager;

	class PipelineCache : public RenderPipelineResolver
	{
	public:
		struct CreateInfo
		{
			RHIPipelineSystem* m_PipelineSystem = nullptr;
			ShaderManager* m_ShaderManager = nullptr;
		};

		explicit PipelineCache(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(PipelineCache);
		~PipelineCache() = default;

		RHIPipelineHandle Resolve(GraphicsPipelineSlot& slot,
			const GraphicsPhysicalPipelineKey& physicalKey,
			const RenderPassInfo& renderPassInfo) noexcept;
		RHIPipelineHandle Resolve(ComputePipelineSlot& slot, const ComputePipelineRecipe& recipe,
			const RenderPassInfo& renderPassInfo) noexcept;
		ShaderManager* GetShaderManager() const noexcept { return m_ShaderManager; }
		void GetPipelineUsages(
			RHIPipelineHandle pipeline, std::vector<RenderPassInfo>& outUsages) const noexcept;

	private:
		void RecordPipelineUsage(
			RHIPipelineHandle pipeline, const RenderPassInfo& renderPassInfo) noexcept;

		ShaderManager* m_ShaderManager = nullptr;
		RHIPipelineSystem* m_PipelineSystem = nullptr;
		mutable std::shared_mutex m_UsageMutex;
		std::unordered_map<RHIPipelineHandle, std::vector<RenderPassInfo>> m_PipelineUsages;
		uint64_t m_UsagePipelineSystemRevision = 0;
	};
}
