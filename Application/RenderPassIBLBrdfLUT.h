#pragma once
#include "RenderPassBase.h"
#include "RenderPassRecipeRegistry.h"
#include "RGResource.h"
#include "RGIBLResources.h"

namespace gglab
{
	class DX12PipelineState;
	class Renderer;
	class RenderPassIBLBrdfLUT : public RenderPassBase
	{
	public:
		RenderPassIBLBrdfLUT() noexcept = default;
		~RenderPassIBLBrdfLUT() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		DX12PipelineState* GetOrCreatePSO(const Renderer& renderer) noexcept;

		static RGTextureDesc BuildRGTextureDescFromNative(const DX12Texture& texture) noexcept;

	private:
		GraphicsKeyInputs m_BaseInputs{};
		bool m_IsInitialized = false;
	};
}