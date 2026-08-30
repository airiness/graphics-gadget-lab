#pragma once
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderPass/ForwardPBRShaderSet.h"
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderQueue.h"

namespace gglab
{
	class RHIGraphicsCommandContext;
	class Renderer;

	class RenderPassDepthPrepass final : public RenderPassBase
	{
	public:
		RenderPassDepthPrepass() noexcept :
			RenderPassBase({
				  .m_TypeName = "Geometry.DepthPrepass",
				  .m_DisplayName = "Depth Prepass",
				  .m_CategoryName = "Geometry",
				  .m_Description =
					  "Clears and writes main-view depth for opaque and alpha-tested geometry.",
				  .m_Category = RenderPassCategory::Geometry,
				  .m_Type = RenderPassType::Graphics,
				})
		{
		}
		~RenderPassDepthPrepass() override = default;

		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

		void Prepare(const RenderServices& services, const ForwardPBRShaderSet& shaderSet) noexcept;

		[[nodiscard]] static std::optional<DepthCoveragePipelineSignature>
			BuildDepthCoveragePipelineSignatureForVariant(
				const GraphicsPhysicalPipelineKey& physicalKey, uint64_t variantBits) noexcept;
		[[nodiscard]] static GraphicsLogicalPipelineMetadata BuildLogicalPipelineMetadataForVariant(
			const GraphicsPhysicalPipelineKey& physicalKey, uint64_t variantBits) noexcept;
		[[nodiscard]] GraphicsPipelineDescription DescribePipelineVariant(
			uint64_t variantBits, bool outputMotion = false) const noexcept;

	private:
		void DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
			const RenderFrameContext& context, const RenderServices& services,
			RenderViewID viewId, bool outputMotion) noexcept;

		void DrawRange(RHIGraphicsCommandContext* graphicsContext, const RenderServices& services,
			const RenderQueue& renderQueue, const DrawItemsRange& range,
			bool outputMotion) noexcept;

		RHIPipelineHandle GetOrCreatePSOForVariant(
			const Renderer& renderer, uint64_t variantBits, bool outputMotion) noexcept;

	private:
		GraphicsPhysicalPipelineKey m_BasePhysicalKey{};
		ShaderID m_AlphaTestPixelShader{};
		ShaderID m_VelocityOpaquePixelShader{};
		ShaderID m_VelocityAlphaTestPixelShader{};
		std::array<std::array<GraphicsPipelineSlot, RenderQueueBuilder::VariantCount>, 2>
			m_PipelineSlots{};
		bool m_IsInitialized = false;
	};
}
