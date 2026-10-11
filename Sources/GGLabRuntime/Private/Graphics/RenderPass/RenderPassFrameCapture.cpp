#include "Graphics/RenderPass/RenderPassFrameCapture.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureAccess.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "Graphics/RenderPass/DiagnosticCaptureGraphResources.h"

#include <optional>

namespace gglab
{
	namespace
	{
		struct PassData
		{
			RGTextureId m_DisplayTarget{};
			RGBufferId m_Readback{};
			RHITextureCopyFootprint m_Footprint{};
		};
	}

	RenderPassFrameCapture::RenderPassFrameCapture(FrameCaptureSource source) noexcept :
		RenderPassBase(MakeInfo(source)), m_Source(source)
	{
	}

	RenderPassInfo RenderPassFrameCapture::MakeInfo(FrameCaptureSource source) noexcept
	{
		switch (source)
		{
		case FrameCaptureSource::Scene:
			return {
				.m_TypeName = "Capture.Scene",
				.m_DisplayName = "Capture Scene",
				.m_CategoryName = "Debug",
				.m_Description =
					"Copies the post-processed display target for queued scene captures.",
				.m_Category = RenderPassCategory::Debug,
				.m_Type = RenderPassType::Transfer,
			};
		case FrameCaptureSource::Composited:
			return {
				.m_TypeName = "Capture.Composited",
				.m_DisplayName = "Capture Composited",
				.m_CategoryName = "Debug",
				.m_Description =
					"Copies the final display target for queued composited captures.",
				.m_Category = RenderPassCategory::Debug,
				.m_Type = RenderPassType::Transfer,
			};
		case FrameCaptureSource::Diagnostic:
			return {
				.m_TypeName = "Capture.Diagnostic",
				.m_DisplayName = "Capture Diagnostic",
				.m_CategoryName = "Debug",
				.m_Description =
					"Copies the display-resolution diagnostic tap target for queued diagnostic captures.",
				.m_Category = RenderPassCategory::Debug,
				.m_Type = RenderPassType::Transfer,
			};
		}
		GGLAB_UNREACHABLE("Unhandled FrameCaptureSource.");
	}

	void RenderPassFrameCapture::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		RenderFrameCaptureAccess* capture = services.m_FrameCapture;
		if (!capture || !capture->HasPendingRequests(m_Source))
		{
			return;
		}

		RHITextureDesc displayTargetDesc{};
		RGTextureId diagnosticTarget{};
		if (m_Source == FrameCaptureSource::Diagnostic)
		{
			// The diagnostic pass of this frame published the target, or reported
			// that the tap has no source and failed its requests.
			const auto* diagnostic = rg.GetBlackboard().TryGet<RGDiagnosticCaptureResources>(
				DiagnosticCaptureResourcesName);
			if (!diagnostic || !diagnostic->IsValid())
			{
				return;
			}
			displayTargetDesc = diagnostic->m_Desc;
			diagnosticTarget = diagnostic->m_Texture;
		}
		else
		{
			const RHISwapChain* swapChain = services.m_Presentation->GetSwapChain();
			GGLAB_ASSERT_NOT_NULL(swapChain);
			displayTargetDesc = {
				.m_Format = swapChain->GetFormat(),
				.m_Usage = swapChain->GetBackBufferUsage(),
				.m_Extent = { swapChain->GetBufferWidth(), swapChain->GetBufferHeight(), 1u },
			};
		}
		const std::optional<FrameCaptureTapTarget> target =
			capture->BindTap(context.m_FrameSerial, m_Source, displayTargetDesc);
		if (!target)
		{
			return;
		}

		const RenderViewID displayViewId = context.GetDisplayViewId();
		rg.AddPass<PassData>(GetRenderGraphPassName(), RGPassEncoderType::Copy,
			[displayViewId, diagnosticTarget, &target = *target](
				RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				if (diagnosticTarget.IsValid())
				{
					data.m_DisplayTarget =
						builder.Read(diagnosticTarget, RGTextureAccess::CopySource);
				}
				else
				{
					auto& targets = builder.GetBlackboard()
						.Get<RGViewTargetsTable>(ViewTargetsTableName)
						.GetViewTargets(displayViewId);
					data.m_DisplayTarget =
						builder.Read(targets.m_BackBuffer, RGTextureAccess::CopySource);
				}
				data.m_Readback = builder.ImportBuffer("FrameCapture.Readback", target.m_Buffer,
					target.m_BufferDesc, RGBufferAccess::CopyDest, RGContentValidity::Undefined);
				builder.WriteInPlace(data.m_Readback, RGBufferAccess::CopyDest, RHIStage::Copy);
				data.m_Footprint = target.m_Footprint;
			},
			[](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetCopyCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				commandContext->CopyTextureToBuffer({
					.m_Source = executeContext.GetTextureHandle(data.m_DisplayTarget),
					.m_Destination = executeContext.GetBufferHandle(data.m_Readback),
					.m_Footprint = data.m_Footprint,
					});
			});
	}
}
