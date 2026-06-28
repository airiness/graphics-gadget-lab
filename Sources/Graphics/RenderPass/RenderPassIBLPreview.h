#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/PipelineCache.h"

namespace gglab
{
	class Renderer;

	class RenderPassIBLPreview : public RenderPassBase
	{
	public:
		RenderPassIBLPreview() noexcept : RenderPassBase({
			.m_TypeName = "Debug.IBLPreview",
			.m_DisplayName = "IBL Preview",
			.m_CategoryName = "Debug",
			.m_Description = "Visualizes generated IBL cubemaps for inspection and learning.",
			.m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Graphics,
		}) {}
		~RenderPassIBLPreview() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		RHIPipelineHandle GetOrCreateCubemapPreviewPSO(const Renderer& renderer) noexcept;

	private:
		GraphicsPipelineRecipe m_CubemapPreviewRecipe{};
		GraphicsPipelineSlot m_CubemapPreviewPipelineSlot{};
		bool m_IsInitialized = false;
	};
}
