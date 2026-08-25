#pragma once

#include "Application/Lab/NapaVoxel/NapaVoxelRenderState.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderPass/RenderPassBase.h"

#include <array>
#include <cstdint>
#include <memory>

namespace gglab
{
	enum class NapaVoxelSurfaceMode : uint8_t
	{
		Shaded,
		Wireframe,
	};
	class NapaVoxelRenderPass final : public RenderPassBase
	{
	public:
		NapaVoxelRenderPass() noexcept;

		void SetFrameView(std::shared_ptr<const NapaVoxelGpuMeshSet> frameView,
			NapaVoxelSurfaceMode surfaceMode) noexcept
		{
			m_FrameView = std::move(frameView);
			m_SurfaceMode = surfaceMode;
		}
		void AddPass(RenderGraph& renderGraph, const RenderFrameContext& frameContext,
			const RenderServices& services) override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		std::shared_ptr<const NapaVoxelGpuMeshSet> m_FrameView;
		std::array<GraphicsPhysicalPipelineKey, 2> m_PipelineKeys{};
		std::array<GraphicsPipelineSlot, 2> m_PipelineSlots{};
		NapaVoxelSurfaceMode m_SurfaceMode = NapaVoxelSurfaceMode::Shaded;
		bool m_IsInitialized = false;
	};
}
