#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Resource/RenderResourceRegistry.h"

namespace gglab
{
	class Renderer;
	class RenderPassIBLPrefilteredSpecular : public RenderPassBase
	{
	public:
		RenderPassIBLPrefilteredSpecular() noexcept : RenderPassBase({
			.m_TypeName = "IBL.PrefilteredSpecular",
			.m_DisplayName = "IBL Prefiltered Specular",
			.m_CategoryName = "IBL",
			.m_Description = "Builds the roughness mip chain used for specular image-based lighting.",
			.m_Category = RenderPassCategory::IBL,
			.m_Type = RenderPassType::Graphics,
		}) {}

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		RHIPipelineHandle GetOrCreatePSO(const Renderer& renderer) noexcept;

	private:
		GraphicsPipelineRecipe m_BaseRecipe{};
		GraphicsPipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
	};
}
