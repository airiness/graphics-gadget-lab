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

		[[nodiscard]] static std::optional<DepthCoveragePipelineSignature>
			BuildDepthCoveragePipelineSignatureForVariant(
				const GraphicsPhysicalPipelineKey& physicalKey,
				uint64_t variantBits) noexcept;
		[[nodiscard]] const GraphicsLogicalPipelineMetadata&
			GetLogicalPipelineMetadataForVariant(
				uint64_t variantBits) const noexcept;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		void DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
			const RenderFrameContext& context,
			const RenderServices& services,
			RenderViewID viewId) noexcept;

		void DrawRange(RHIGraphicsCommandContext* graphicsContext,
			const RenderServices& services,
			const RenderQueue& renderQueue,
			const DrawItemsRange& range) noexcept;

		RHIPipelineHandle GetOrCreatePSOForVariant(
			const Renderer& renderer,
			uint64_t variantBits) noexcept;

		std::tuple<RasterizerPreset, DepthPreset, BlendPreset>
			GetPresetsFromVariantBits(uint64_t variantBits) const noexcept;

	private:
		GraphicsPhysicalPipelineKey m_BasePhysicalKey{};
		std::array<GraphicsPipelineSlot, RenderQueueBuilder::VariantCount> m_PipelineSlots{};
		bool m_IsInitialized = false;
	};
}
