#pragma once

#include "Application/Lab/NapaVoxel/NapaVoxelRenderState.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderPass/RenderPassBase.h"

#include <memory>

namespace gglab
{
	class NapaVoxelRenderPass final : public RenderPassBase
	{
	public:
		NapaVoxelRenderPass() noexcept;

		void SetFrameView(std::shared_ptr<const NapaVoxelGpuMeshSet> frameView) noexcept
		{
			m_FrameView = std::move(frameView);
		}
		void AddPass(RenderGraph& renderGraph, const RenderFrameContext& frameContext,
			const RenderServices& services) override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		std::shared_ptr<const NapaVoxelGpuMeshSet> m_FrameView;
		GraphicsPhysicalPipelineKey m_PipelineKey{};
		GraphicsPipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
	};
}
