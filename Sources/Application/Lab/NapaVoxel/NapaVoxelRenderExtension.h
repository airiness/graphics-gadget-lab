#pragma once

#include "Application/Lab/NapaVoxel/NapaVoxelRenderPass.h"
#include "Graphics/RenderPipeline/RenderPipelineSceneExtensionBase.h"

namespace gglab
{
	class NapaVoxelRenderFrameSource final
	{
	public:
		void SetFrameView(std::shared_ptr<const NapaVoxelGpuMeshSet> frameView) noexcept
		{
			m_FrameView = std::move(frameView);
		}
		void ClearFrameView() noexcept { m_FrameView.reset(); }
		[[nodiscard]] std::shared_ptr<const NapaVoxelGpuMeshSet> GetFrameView() const noexcept
		{
			return m_FrameView;
		}

	private:
		std::shared_ptr<const NapaVoxelGpuMeshSet> m_FrameView;
	};

	class NapaVoxelRenderExtension final : public RenderPipelineSceneExtensionBase
	{
	public:
		explicit NapaVoxelRenderExtension(
			std::shared_ptr<NapaVoxelRenderFrameSource> frameSource) noexcept :
			m_FrameSource(std::move(frameSource))
		{
		}

		void AddOpaqueScenePasses(RenderGraph& renderGraph,
			const RenderFrameContext& frameContext,
			const RenderServices& services) noexcept override;

	private:
		std::shared_ptr<NapaVoxelRenderFrameSource> m_FrameSource;
		NapaVoxelRenderPass m_RenderPass;
	};
}
