#pragma once
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/ForwardPBRShaderSet.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"

#include <array>
#include <memory>

namespace gglab
{
	enum class ForwardPBRPassKind : uint8_t
	{
		Opaque,
		Transparent,
	};

	enum class ForwardPBRLightingVariant : uint8_t
	{
		AllLights,
		ForwardPlus,
		ForwardPlusValidation,
		Count,
	};

	// Opaque shading always consumes Forward+ light lists; transparent shading evaluates
	// all lights. HDR-diff validation is active only when the pipeline composed the
	// Lab-owned validation recipe and published an active validation record for this frame.
	[[nodiscard]] constexpr ForwardPBRLightingVariant ResolveForwardPBRLightingVariant(
		ForwardPBRPassKind passKind, bool hdrDiffValidationActive) noexcept
	{
		if (passKind == ForwardPBRPassKind::Transparent)
		{
			return ForwardPBRLightingVariant::AllLights;
		}
		return hdrDiffValidationActive
			? ForwardPBRLightingVariant::ForwardPlusValidation
			: ForwardPBRLightingVariant::ForwardPlus;
	}

	class RHIGraphicsCommandContext;
	class RenderPassForwardPBRBase : public RenderPassBase
	{
	public:
		~RenderPassForwardPBRBase() override = default;

		void Prepare(const RenderServices& services, const ForwardPBRShaderSet& shaderSet) noexcept;
		[[nodiscard]] bool PrewarmMaterialDiagnosticVariant(const RenderServices& services,
			uint64_t variantBits, ForwardPBRLightingVariant lightingVariant,
			bool gtaoContributionOutputEnabled) noexcept;

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
			const RenderQueue* expectedRenderQueue, ForwardPBRLightingVariant lightingVariant,
			bool gtaoContributionOutputEnabled, bool materialDiagnostics) noexcept;

		void DrawRange(RHIGraphicsCommandContext* graphicsContext, const RenderServices& services,
			const RenderQueue& renderQueue, const DrawItemsRange& range,
			const RenderQueue* expectedRenderQueue,
			ForwardPBRLightingVariant lightingVariant,
			bool gtaoContributionOutputEnabled, bool materialDiagnostics) noexcept;

		RHIPipelineHandle GetOrCreatePSOForVariant(
			const RenderServices& services, uint64_t variantBits,
			ForwardPBRLightingVariant lightingVariant,
			bool gtaoContributionOutputEnabled, bool materialDiagnostics) noexcept;

		// Opaque draws test EQUAL against the depth prepass; transparent draws only read depth.
		std::tuple<RasterizerPreset, DepthPreset, BlendPreset> GetPresetsFromVariantBits(
			uint64_t variantBits) const noexcept;

		[[nodiscard]] ForwardPBRLightingVariant GetBaseLightingVariant() const noexcept;

	private:
		static constexpr size_t LightingVariantCount =
			static_cast<size_t>(ForwardPBRLightingVariant::Count);
		static constexpr size_t GTAOContributionVariantCount = 2;
		static constexpr size_t OutputVariantCount = 4;

		ForwardPBRPassKind m_PassKind = ForwardPBRPassKind::Opaque;
		std::array<std::array<GraphicsPhysicalPipelineKey, OutputVariantCount>,
			LightingVariantCount> m_BasePhysicalKeys{};
		using PipelineSlotTable = std::array<std::array<std::array<GraphicsPipelineSlot,
			RenderQueueBuilder::VariantCount>, GTAOContributionVariantCount>, LightingVariantCount>;
		PipelineSlotTable m_PipelineSlots{};
		std::unique_ptr<PipelineSlotTable> m_MaterialDiagnosticPipelineSlots;
		bool m_IsInitialized = false;
	};
}
