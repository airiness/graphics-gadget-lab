#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/RenderPassRecipeRegistry.h"
#include "Graphics/RenderQueue.h"

namespace gglab
{
	class DX12CommandList;
	class DX12PipelineState;
	class RenderPassForwardPBR : public RenderPassBase
	{
	public:
		RenderPassForwardPBR() noexcept = default;
		~RenderPassForwardPBR() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		void DrawRenderQueue(DX12CommandList* commandList,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept;

		void DrawRange(DX12CommandList* commandList,
			const RenderFrameContext& context,
			const RenderServices& services,
			const DrawItemsRange& range) noexcept;

		DX12PipelineState* GetOrCreatePSOForVariant(
			const Renderer& renderer,
			uint64_t variantBits) noexcept;

		std::tuple<RasterizerPreset, DepthPreset, BlendPreset>
			GetPresetsFromVariantBits(uint64_t variantBits) const noexcept;

		std::string MakeVariantPSOPassId(std::string_view baseName, uint64_t variantBits) const noexcept;

	private:
		GraphicsKeyInputs m_BaseInputs{};
		bool m_IsInitialized = false;
	};
}