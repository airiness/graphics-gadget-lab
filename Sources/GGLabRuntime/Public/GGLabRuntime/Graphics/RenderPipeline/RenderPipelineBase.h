#pragma once
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

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

	enum class RenderFrameValidationStatus : uint8_t
	{
		Ready,
		// Expected lifecycle state: the frame is skipped without rendering or
		// committing temporal history, and the next frame may proceed.
		Skipped,
		// The selected pipeline's contract is violated. The owning host must not
		// resume this rendering path or substitute another rendering algorithm.
		ContractFailure,
	};

	struct RenderFrameValidationResult
	{
		RenderFrameValidationStatus m_Status = RenderFrameValidationStatus::Ready;
		std::string_view m_Reason;
		std::string m_Detail;

		[[nodiscard]] static RenderFrameValidationResult Ready() noexcept { return {}; }
		[[nodiscard]] static RenderFrameValidationResult Skip(std::string_view reason) noexcept
		{
			return { .m_Status = RenderFrameValidationStatus::Skipped, .m_Reason = reason };
		}
		[[nodiscard]] static RenderFrameValidationResult ContractFailure(
			std::string_view reason, std::string detail = {}) noexcept
		{
			return { .m_Status = RenderFrameValidationStatus::ContractFailure,
				.m_Reason = reason, .m_Detail = std::move(detail) };
		}

		[[nodiscard]] bool IsReady() const noexcept
		{
			return m_Status == RenderFrameValidationStatus::Ready;
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

		// Called once per frame after the host frame build and before graph
		// construction. A pipeline classifies the frame before any work is
		// recorded; only a Ready frame may be built with the same frame context.
		[[nodiscard]] virtual RenderFrameValidationResult ValidateRenderFrame(
			const RenderFrameContext&, const RenderServices&) noexcept
		{
			return RenderFrameValidationResult::Ready();
		}
		virtual void BuildRenderGraph(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept = 0;
	};
}
