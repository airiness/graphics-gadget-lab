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

		void Prepare(const RenderServices& services) noexcept;

		[[nodiscard]] static std::optional<DepthCoveragePipelineSignature>
			BuildDepthCoveragePipelineSignatureForVariant(
				const GraphicsPhysicalPipelineKey& physicalKey,
				uint64_t variantBits) noexcept;
		[[nodiscard]] static GraphicsLogicalPipelineMetadata
			BuildLogicalPipelineMetadataForVariant(
				const GraphicsPhysicalPipelineKey& physicalKey,
				uint64_t variantBits) noexcept;
		[[nodiscard]] GraphicsPipelineDescription
			DescribePipelineVariant(uint64_t variantBits) const noexcept;

	private:
		struct DepthEqualVariantValidation
		{
			std::optional<DepthCoveragePipelineSignature>
				m_PrepassSignature;
			std::optional<DepthCoveragePipelineSignature>
				m_ForwardSignature;
			bool m_Matches = false;
		};

		void AddBucketPass(
			RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services,
			bool transparent) noexcept;

		void DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
			const RenderFrameContext& context,
			const RenderServices& services,
			RenderViewID viewId,
			bool transparent,
			const RenderQueue* expectedRenderQueue,
			bool useDepthEqual) noexcept;

		void DrawRange(RHIGraphicsCommandContext* graphicsContext,
			const RenderServices& services,
			const RenderQueue& renderQueue,
			const DrawItemsRange& range,
			bool useDepthEqual,
			const RenderQueue* expectedRenderQueue) noexcept;

		RHIPipelineHandle GetOrCreatePSOForVariant(
			const Renderer& renderer,
			uint64_t variantBits,
			bool useDepthEqual) noexcept;

		std::tuple<RasterizerPreset, DepthPreset, BlendPreset>
			GetPresetsFromVariantBits(
				uint64_t variantBits,
				bool useDepthEqual) const noexcept;
		[[nodiscard]] bool ValidateDepthEqualVariant(
			uint64_t variantBits,
			const std::optional<
				DepthCoveragePipelineSignature>&
				prepassSignature,
			const std::optional<
				DepthCoveragePipelineSignature>&
				forwardSignature) noexcept;

	private:
		GraphicsPhysicalPipelineKey m_BasePhysicalKey{};
		std::array<GraphicsPipelineSlot, RenderQueueBuilder::VariantCount> m_PipelineSlots{};
		std::array<
			DepthEqualVariantValidation,
			RenderQueueBuilder::VariantCount>
			m_DepthEqualVariantValidations{};
		bool m_IsInitialized = false;
	};
}
