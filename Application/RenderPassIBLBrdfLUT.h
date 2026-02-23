#pragma once
#include "RenderPassBase.h"
#include "RenderPassRecipeRegistry.h"
#include "RGResource.h"

namespace gglab
{
	struct RGIBLResources
	{
		RGTextureId m_BrdfLut{};
	};
	inline constexpr const char* IBLResourcesName = "RGIBLResources";

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