#pragma once

#include "Graphics/Pipeline/GTAO.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderPass/RenderPassBase.h"

#include <array>

namespace gglab
{
	class Renderer;

	class RenderPassGTAO final : public RenderPassBase
	{
	public:
		RenderPassGTAO() noexcept :
			RenderPassBase({
				  .m_TypeName = "Lighting.GTAO",
				  .m_DisplayName = "GTAO",
				  .m_CategoryName = "Lighting",
				  .m_Description =
					  "Evaluates, denoises, and upsamples ambient occlusion from display depth.",
				  .m_Category = RenderPassCategory::Lighting,
				  .m_Type = RenderPassType::Compute,
				})
		{
		}
		~RenderPassGTAO() override = default;

		void Prepare(const RenderServices& services) noexcept;
		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;
		[[nodiscard]] bool IsAvailable() const noexcept { return m_IsAvailable; }
		[[nodiscard]] const GTAOCapabilityStatus& GetCapabilityStatus() const noexcept
		{
			return m_Capabilities;
		}

	private:
		enum class PipelineVariant : uint8_t
		{
			Evaluate,
			EvaluateDiagnostics,
			DenoiseX,
			DenoiseY,
			Upsample,
			Count,
		};

		[[nodiscard]] RHIPipelineHandle GetOrCreatePipeline(
			const Renderer& renderer, PipelineVariant variant) noexcept;

		std::array<ComputePipelineRecipe, static_cast<size_t>(PipelineVariant::Count)>
			m_PipelineRecipes{};
		std::array<ComputePipelineSlot, static_cast<size_t>(PipelineVariant::Count)>
			m_PipelineSlots{};
		GTAOCapabilityStatus m_Capabilities{};
		bool m_IsInitialized = false;
		bool m_IsAvailable = false;
		bool m_DiagnosticPipelineAvailable = false;
	};
}
