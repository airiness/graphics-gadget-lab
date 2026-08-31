#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/ForwardPBRShaderSet.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "GGLabRuntime/Graphics/PostProcess/ViewRenderSettings.h"
#include "Graphics/RenderQueue.h"

namespace gglab
{
	enum class ForwardPBRPassKind : uint8_t
	{
		Opaque,
		Transparent,
	};

	enum class ForwardPBRLightingVariant : uint8_t
	{
		Legacy,
		ForwardPlus,
		ForwardPlusValidation,
		Count,
	};

	[[nodiscard]] constexpr ForwardPBRLightingVariant ResolveForwardPBRLightingVariant(
		ForwardPBRPassKind passKind, const ForwardPlusSettings& settings,
		bool hdrDiffValidationAvailable) noexcept
	{
		if (passKind == ForwardPBRPassKind::Transparent ||
			settings.m_Mode == ForwardLightingMode::Legacy)
		{
			return ForwardPBRLightingVariant::Legacy;
		}
		return settings.m_EnableHdrDiffValidation && hdrDiffValidationAvailable
			? ForwardPBRLightingVariant::ForwardPlusValidation
			: ForwardPBRLightingVariant::ForwardPlus;
	}

	class RHIGraphicsCommandContext;
	class RenderPassForwardPBRBase : public RenderPassBase
	{
	public:
		~RenderPassForwardPBRBase() override = default;

		void Prepare(const RenderServices& services, const ForwardPBRShaderSet& shaderSet) noexcept;
		void SetHdrDiffValidationAvailable(bool available) noexcept
		{
			m_HdrDiffValidationAvailable = available;
		}

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
			const RenderQueue* expectedRenderQueue, bool useDepthEqual,
			ForwardPBRLightingVariant lightingVariant,
			bool gtaoContributionOutputEnabled) noexcept;

		void DrawRange(RHIGraphicsCommandContext* graphicsContext, const RenderServices& services,
			const RenderQueue& renderQueue, const DrawItemsRange& range, bool useDepthEqual,
			const RenderQueue* expectedRenderQueue,
			ForwardPBRLightingVariant lightingVariant,
			bool gtaoContributionOutputEnabled) noexcept;

		RHIPipelineHandle GetOrCreatePSOForVariant(
			const Renderer& renderer, uint64_t variantBits, bool useDepthEqual,
			ForwardPBRLightingVariant lightingVariant,
			bool gtaoContributionOutputEnabled) noexcept;

		std::tuple<RasterizerPreset, DepthPreset, BlendPreset> GetPresetsFromVariantBits(
			uint64_t variantBits, bool useDepthEqual) const noexcept;

	private:
		static constexpr size_t LightingVariantCount =
			static_cast<size_t>(ForwardPBRLightingVariant::Count);
		static constexpr size_t GTAOContributionVariantCount = 2;

		ForwardPBRPassKind m_PassKind = ForwardPBRPassKind::Opaque;
		std::array<std::array<GraphicsPhysicalPipelineKey, GTAOContributionVariantCount>,
			LightingVariantCount> m_BasePhysicalKeys{};
		std::array<std::array<std::array<GraphicsPipelineSlot, RenderQueueBuilder::VariantCount>,
			GTAOContributionVariantCount>, LightingVariantCount> m_PipelineSlots{};
		bool m_HdrDiffValidationAvailable = false;
		bool m_IsInitialized = false;
	};
}
