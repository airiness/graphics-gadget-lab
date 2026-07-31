#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/ForwardPBRShaderSet.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderQueue.h"

namespace gglab
{
	enum class ForwardPBRPassKind : uint8_t
	{
		Opaque,
		Transparent,
	};

	class RHIGraphicsCommandContext;
	class RenderPassForwardPBRBase : public RenderPassBase
	{
	public:
		~RenderPassForwardPBRBase() override = default;

		void Prepare(const RenderServices& services, const ForwardPBRShaderSet& shaderSet) noexcept;

		[[nodiscard]] static std::optional<DepthCoveragePipelineSignature>
			BuildDepthCoveragePipelineSignatureForVariant(
				const GraphicsPhysicalPipelineKey& physicalKey, uint64_t variantBits) noexcept;
		[[nodiscard]] static GraphicsLogicalPipelineMetadata BuildLogicalPipelineMetadataForVariant(
			const GraphicsPhysicalPipelineKey& physicalKey, uint64_t variantBits) noexcept;
		[[nodiscard]] GraphicsPipelineDescription DescribePipelineVariant(
			uint64_t variantBits) const noexcept;

	protected:
		RenderPassForwardPBRBase(RenderPassInfo info, ForwardPBRPassKind passKind) noexcept :
			RenderPassBase(std::move(info)), m_PassKind(passKind)
		{
		}

		void AddForwardPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept;

	private:
		void DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
			const RenderFrameContext& context, const RenderServices& services, RenderViewID viewId,
			const RenderQueue* expectedRenderQueue, bool useDepthEqual) noexcept;

		void DrawRange(RHIGraphicsCommandContext* graphicsContext, const RenderServices& services,
			const RenderQueue& renderQueue, const DrawItemsRange& range, bool useDepthEqual,
			const RenderQueue* expectedRenderQueue) noexcept;

		RHIPipelineHandle GetOrCreatePSOForVariant(
			const Renderer& renderer, uint64_t variantBits, bool useDepthEqual) noexcept;

		std::tuple<RasterizerPreset, DepthPreset, BlendPreset> GetPresetsFromVariantBits(
			uint64_t variantBits, bool useDepthEqual) const noexcept;

	private:
		ForwardPBRPassKind m_PassKind = ForwardPBRPassKind::Opaque;
		GraphicsPhysicalPipelineKey m_BasePhysicalKey{};
		std::array<GraphicsPipelineSlot, RenderQueueBuilder::VariantCount> m_PipelineSlots{};
		bool m_IsInitialized = false;
	};
}
