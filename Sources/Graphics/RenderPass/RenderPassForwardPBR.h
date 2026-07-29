#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderQueue.h"

namespace gglab
{
	class RHIGraphicsCommandContext;
	class RenderPassForwardPBR : public RenderPassBase
	{
	public:
		RenderPassForwardPBR() noexcept : RenderPassBase({
			.m_TypeName = "Geometry.ForwardPBR",
			.m_DisplayName = "Forward PBR",
			.m_CategoryName = "Geometry",
			.m_Description = "Renders opaque, alpha-tested and transparent scene geometry with forward PBR shading.",
			.m_Category = RenderPassCategory::Geometry,
			.m_Type = RenderPassType::Graphics,
		}) {}
		~RenderPassForwardPBR() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

		[[nodiscard]] static std::optional<DepthCoverageSignature>
			BuildDepthCoverageSignatureForVariant(
				const GraphicsPipelineRecipe& recipe,
				uint64_t variantBits) noexcept;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		void DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
			const RenderFrameContext& context,
			const RenderServices& services,
			RenderViewID viewId) noexcept;

		void DrawRange(RHIGraphicsCommandContext* graphicsContext,
			const RenderFrameContext& context,
			const RenderServices& services,
			const RenderQueue& renderQueue,
			const DrawItemsRange& range) noexcept;

		RHIPipelineHandle GetOrCreatePSOForVariant(
			const Renderer& renderer,
			uint64_t variantBits) noexcept;

		[[nodiscard]] static DepthCoverageBinding BuildDepthCoverageBindingForDraw(
			const Renderer& renderer,
			const RenderFrameContext& context,
			const DrawItem& drawItem,
			RenderViewID viewId) noexcept;

		std::tuple<RasterizerPreset, DepthPreset, BlendPreset>
			GetPresetsFromVariantBits(uint64_t variantBits) const noexcept;

	private:
		GraphicsPipelineRecipe m_BaseRecipe{};
		std::array<GraphicsPipelineSlot, RenderQueueBuilder::VariantCount> m_PipelineSlots{};
		bool m_IsInitialized = false;
	};
}
