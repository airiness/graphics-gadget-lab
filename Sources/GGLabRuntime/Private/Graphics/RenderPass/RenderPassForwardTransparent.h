#pragma once
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"
#include "Graphics/RenderPass/ForwardPBRShaderSet.h"

#include <array>
#include <cstdint>
#include <memory>

namespace gglab
{
	// Blends transparent draws over scene color after the opaque stages, testing
	// against prepass depth without writing it. Lighting evaluates every scene light
	// and does not depend on Forward+ light lists.
	class RenderPassForwardTransparent final : public RenderPassBase
	{
	public:
		RenderPassForwardTransparent() noexcept :
			RenderPassBase({
				.m_TypeName = "Geometry.ForwardTransparent",
				.m_DisplayName = "Forward Transparent",
				.m_CategoryName = "Geometry",
				.m_Description =
					"Renders transparent scene geometry with all-lights forward PBR shading.",
				.m_Category = RenderPassCategory::Geometry,
				.m_Type = RenderPassType::Graphics,
			})
		{
		}
		~RenderPassForwardTransparent() override = default;

		void Prepare(const RenderServices& services, const ForwardPBRShaderSet& shaderSet) noexcept;
		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

		[[nodiscard]] bool PrewarmMaterialDiagnosticVariant(
			const RenderServices& services, uint64_t variantBits) noexcept;

	private:
		[[nodiscard]] RHIPipelineHandle GetOrCreatePSOForVariant(const RenderServices& services,
			uint64_t variantBits, bool materialDiagnostics) noexcept;

		// Indexed by material diagnostics output.
		std::array<GraphicsPhysicalPipelineKey, 2> m_PhysicalKeys{};
		using PipelineSlotTable = std::array<GraphicsPipelineSlot, RenderQueueBuilder::VariantCount>;
		PipelineSlotTable m_PipelineSlots{};
		std::unique_ptr<PipelineSlotTable> m_MaterialDiagnosticPipelineSlots;
		bool m_IsInitialized = false;
	};
}
