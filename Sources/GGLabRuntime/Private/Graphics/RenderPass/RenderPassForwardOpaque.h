#pragma once
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"
#include "Graphics/RenderPass/ForwardPBRShaderSet.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace gglab
{
	// Shades opaque and alpha-tested draws with Forward+ light lists after the EQUAL
	// depth prepass. Requires this frame's RGForwardPlusResources; a Lab-composed
	// HDR-diff recipe additionally writes an all-lights reference target.
	class RenderPassForwardOpaque final : public RenderPassBase
	{
	public:
		RenderPassForwardOpaque() noexcept :
			RenderPassBase({
				.m_TypeName = "Geometry.ForwardOpaque",
				.m_DisplayName = "Forward Opaque",
				.m_CategoryName = "Geometry",
				.m_Description =
					"Renders opaque and alpha-tested scene geometry with Forward+ PBR shading.",
				.m_Category = RenderPassCategory::Geometry,
				.m_Type = RenderPassType::Graphics,
			})
		{
		}
		~RenderPassForwardOpaque() override = default;

		void Prepare(const RenderServices& services, const ForwardPBRShaderSet& shaderSet) noexcept;
		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

		[[nodiscard]] bool PrewarmMaterialDiagnosticVariant(const RenderServices& services,
			uint64_t variantBits, bool hdrDiffValidation, bool gtaoContributionOutput) noexcept;

		[[nodiscard]] static std::optional<DepthCoveragePipelineSignature>
			BuildDepthCoveragePipelineSignatureForVariant(
				const GraphicsPhysicalPipelineKey& physicalKey, uint64_t variantBits) noexcept;
		[[nodiscard]] static GraphicsLogicalPipelineMetadata BuildLogicalPipelineMetadataForVariant(
			const GraphicsPhysicalPipelineKey& physicalKey, uint64_t variantBits) noexcept;
		[[nodiscard]] GraphicsPipelineDescription DescribePipelineVariant(
			uint64_t variantBits) const noexcept;

	private:
		enum class LightingRecipe : uint8_t
		{
			ForwardPlus,
			ForwardPlusValidation,
			Count,
		};

		struct OutputVariant
		{
			LightingRecipe m_Lighting = LightingRecipe::ForwardPlus;
			bool m_GTAOContribution = false;
			bool m_MaterialDiagnostics = false;
		};

		[[nodiscard]] RHIPipelineHandle GetOrCreatePSOForVariant(const RenderServices& services,
			uint64_t variantBits, const OutputVariant& output) noexcept;

		static constexpr size_t LightingRecipeCount = static_cast<size_t>(LightingRecipe::Count);
		// Indexed by lighting recipe, GTAO contribution output and material diagnostics.
		std::array<std::array<std::array<GraphicsPhysicalPipelineKey, 2>, 2>, LightingRecipeCount>
			m_PhysicalKeys{};
		using PipelineSlotTable = std::array<std::array<std::array<GraphicsPipelineSlot,
			RenderQueueBuilder::VariantCount>, 2>, LightingRecipeCount>;
		PipelineSlotTable m_PipelineSlots{};
		// Optional diagnostic variants must not double the pass object's inline cache.
		std::unique_ptr<PipelineSlotTable> m_MaterialDiagnosticPipelineSlots;
		bool m_IncludesHdrDiffValidation = false;
		bool m_IsInitialized = false;
	};
}
