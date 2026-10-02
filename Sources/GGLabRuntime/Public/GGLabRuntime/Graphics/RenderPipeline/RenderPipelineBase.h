#pragma once
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace gglab
{
	struct RenderView;
	struct RenderScene;
	class RenderGraph;

	struct MaterialDiagnosticPrewarmProgress
	{
		uint32_t m_CompletedCount = 0;
		uint32_t m_TotalCount = 0;
		bool m_Failed = false;

		[[nodiscard]] bool IsReady() const noexcept
		{
			return !m_Failed && m_CompletedCount == m_TotalCount;
		}
	};

	class RenderPipelineBase
	{
	public:
		RenderPipelineBase() noexcept = default;
		virtual ~RenderPipelineBase() = default;

		virtual std::string_view GetName() const noexcept = 0;
		virtual void PrepareTemporalFramePlanning(const RenderServices&) noexcept {}

		virtual ResolvedTemporalFramePlan ResolveTemporalFramePlan(
			TemporalFramePlanResolveInfo info) const noexcept
		{
			info.m_DepthVelocityPathAvailable = false;
			info.m_SceneExtensionParticipation =
				SceneExtensionTemporalParticipation::TemporalUnsupported;
			return gglab::ResolveTemporalFramePlan(info);
		}

		// Called on the RHI owner thread after shader artifacts are published.
		// Each invocation resolves at most one pipeline for the supplied unique draw variants.
		[[nodiscard]] virtual MaterialDiagnosticPrewarmProgress PrewarmMaterialDiagnostics(
			const RenderServices&, std::span<const uint64_t>) noexcept
		{
			return { .m_Failed = true };
		}

		virtual void BuildRenderGraph(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept = 0;
		[[nodiscard]] virtual bool ValidateRenderFrame(
			const RenderFrameContext&, const RenderServices&) noexcept
		{
			return true;
		}
	};
}
