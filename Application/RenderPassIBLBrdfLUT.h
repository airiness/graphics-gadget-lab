#pragma once
#include "RenderPassBase.h"
#include "RenderPassRecipeRegistry.h"

namespace gglab
{
	class DX12PipelineState;
	class RenderPassIBLBrdfLUT : public RenderPassBase
	{
	public:
		RenderPassIBLBrdfLUT() noexcept = default;
		~RenderPassIBLBrdfLUT() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsuredInitialized(const RenderServices& services) noexcept;

		DX12PipelineState* GetOrCreatePSO(const Renderer& renderer) noexcept;

	private:
		GraphicsKeyInputs m_BaseInputs{};
		bool m_IsInitialized = false;
	};
}