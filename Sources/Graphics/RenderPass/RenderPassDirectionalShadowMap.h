#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/PipelineCache.h"
#include "Graphics/RenderQueue.h"

namespace gglab
{
	class RHIGraphicsCommandContext;
	class DX12CommandList;
	class DX12PipelineState;
	class Renderer;
	struct DirectionalShadowSettings;

	class RenderPassDirectionalShadowMap final : public RenderPassBase
	{
	public:
		RenderPassDirectionalShadowMap() noexcept = default;
		~RenderPassDirectionalShadowMap() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		void DrawRenderQueue(DX12CommandList* commandList,
			RHIGraphicsCommandContext* graphicsContext,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept;

		void DrawRange(DX12CommandList* commandList,
			RHIGraphicsCommandContext* graphicsContext,
			const RenderFrameContext& context,
			const RenderServices& services,
			const RenderQueue& renderQueue,
			const DrawItemsRange& range) noexcept;

		DX12PipelineState* GetOrCreatePSOForVariant(
			const Renderer& renderer,
			uint64_t variantBits,
			const DirectionalShadowSettings& shadowSettings) noexcept;

		RasterizerPreset GetRasterizerPresetFromVariantBits(uint64_t variantBits) const noexcept;

	private:
		GraphicsPipelineRecipe m_BaseRecipe{};
		std::array<GraphicsPipelineSlot, RenderQueueBuilder::VariantCount> m_PipelineSlots{};
		bool m_IsInitialized = false;
	};
}
