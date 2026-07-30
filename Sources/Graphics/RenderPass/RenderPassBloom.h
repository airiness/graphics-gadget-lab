#pragma once
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/PostProcess/PostProcessColor.h"
#include "Graphics/PostProcess/PostProcessDebug.h"
#include "Graphics/RenderPass/RenderPassBase.h"

#include <functional>

namespace gglab
{
	class Renderer;

	class RenderPassBloom final : public RenderPassBase
	{
	public:
		using DebugTapCallback = std::function<void(
			const RGPostProcessColor& source,
			PostProcessDebugTap tap,
			uint32_t bloomPyramidLevel)>;

		RenderPassBloom() noexcept : RenderPassBase({
			.m_TypeName = "PostProcess.Bloom",
			.m_DisplayName = "Bloom",
			.m_CategoryName = "PostProcess",
			.m_Description = "Builds and reconstructs a low-resolution HDR bloom pyramid.",
			.m_Category = RenderPassCategory::PostProcess,
			.m_Type = RenderPassType::Graphics,
		}) {}
		~RenderPassBloom() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;
		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services,
			const DebugTapCallback& debugTapCallback) noexcept;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;
		RHIPipelineHandle GetOrCreatePSO(
			const Renderer& renderer,
			RHIFormat renderTargetFormat,
			bool additive) noexcept;

		GraphicsPhysicalPipelineKey m_BaseRecipe{};
		GraphicsPipelineSlot m_FilterPipelineSlot{};
		GraphicsPipelineSlot m_AdditivePipelineSlot{};
		bool m_IsInitialized = false;
	};
}
