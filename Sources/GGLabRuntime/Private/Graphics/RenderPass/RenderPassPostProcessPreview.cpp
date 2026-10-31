#include "Graphics/RenderPass/RenderPassPostProcessPreview.h"
#include "GGLabRuntime/Graphics/RenderParameters.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchDefaults.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/PostProcess/PostProcessGraphResources.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/RenderPass/GIPreviewGraphResources.h"
#include "Graphics/GIShaderConstants.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewGraphResources.h"
#include "Graphics/RenderPass/AtmosphereGraphResources.h"
#include "Graphics/RenderPass/AerialPerspectiveGraphResources.h"
#include "Graphics/RenderPass/DiagnosticCaptureGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPass/TemporalGeometryGraphResources.h"
#include "Graphics/RenderPass/TemporalAAGraphResources.h"
#include "Graphics/RenderPass/TemporalReferenceGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace gglab
{
	namespace
	{
		struct PostProcessPreviewPassParameters
		{
			uint32_t SourceTextureIndex = 0;
			uint32_t SourceSamplerIndex = 0;
			uint32_t ViewIndex = 0;
			uint32_t SourceMode = 0;
			float SourcePreExposure = 1.0f;
			float PreviewExposureScale = 1.0f;
			uint32_t ValidityTextureIndex = UINT32_MAX;
			uint32_t ActivityTextureIndex = UINT32_MAX;
		};
		static_assert(IsPassConstantBufferStruct<PostProcessPreviewPassParameters>);
		static_assert(sizeof(PostProcessPreviewPassParameters) == 32);
		static_assert(offsetof(PostProcessPreviewPassParameters, ValidityTextureIndex) == 24);
		static_assert(offsetof(PostProcessPreviewPassParameters, ActivityTextureIndex) == 28);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::SceneDepthRaw) == 4);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::SceneDepthLinearViewZ) == 5);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAORawAO) == 6);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAOHalfDepthViewZ) == 7);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAOReconstructedNormal) == 8);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAOSelectedSurfaceOffset) == 9);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAODenoiseX) == 10);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAODenoiseY) == 11);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAOFinalAO) == 12);
		static_assert(static_cast<uint32_t>(
			PostProcessDebugTap::GTAOAOOnlyLightingContribution) == 13);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::TemporalMotionDirection) == 14);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::TemporalMotionMagnitude) == 15);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::TemporalHistoryColor) == 16);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::TemporalReprojectionUV) == 17);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::TemporalRejection) == 18);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::TemporalHistoryWeight) == 19);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::TemporalHistorySamples) == 20);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::TemporalClipDistance) == 26);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::TemporalHistoryRelaxation) == 27);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAOHistorySamples) == 28);

		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GIIrradiance) == gi_shader::GIPreviewIrradiance);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GILambertRadiance) == gi_shader::GIPreviewLambertRadiance);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GISunIrradiance) == gi_shader::GIPreviewSunIrradiance);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GIEnvironmentIrradiance) == gi_shader::GIPreviewEnvironmentIrradiance);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GIBaseVisibility) == gi_shader::GIPreviewBaseVisibility);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GICoatVisibility) == gi_shader::GIPreviewCoatVisibility);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GICombinedVisibility) == gi_shader::GIPreviewCombinedVisibility);

		struct PreviewBuildState
		{
			std::array<bool, static_cast<std::size_t>(PostProcessPreviewChannel::Count)> m_Added{};
		};

		struct PassData
		{
			RGTextureId m_Source{};
			RGTextureId m_Output{};
			RGTextureViewId m_SourceSrv{};
			RGTextureViewId m_ValiditySrv{};
			RGTextureViewId m_ActivitySrv{};
			RGTextureViewId m_OutputRtv{};
			PostProcessDebugSelection m_Selection{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_SamplerIndex = 0;
			float m_SourcePreExposure = 1.0f;
			float m_PreviewExposureScale = 1.0f;
		};

		RGPostProcessColor ResolvePreviewSource(
			const RGPostProcessResources& resources, PostProcessDebugSelection selection) noexcept
		{
			switch (selection.m_Tap)
			{
			case PostProcessDebugTap::SceneColor:
				return resources.m_Inputs.m_SceneColor;
			case PostProcessDebugTap::BloomResult:
				return resources.m_Bloom.m_Result;
			default:
				return {};
			}
		}

		bool IsGIPreview(PostProcessDebugTap tap) noexcept
		{
			return IsGIDiffusePreviewTap(tap) || IsGIVisibilityPreviewTap(tap);
		}

		bool IsDepthPreview(PostProcessDebugTap tap) noexcept
		{
			return tap == PostProcessDebugTap::SceneDepthRaw ||
				tap == PostProcessDebugTap::SceneDepthLinearViewZ;
		}

		bool IsGTAOPreview(PostProcessDebugTap tap) noexcept
		{
			return tap == PostProcessDebugTap::GTAORawAO ||
				tap == PostProcessDebugTap::GTAOHalfDepthViewZ ||
				tap == PostProcessDebugTap::GTAOReconstructedNormal ||
				tap == PostProcessDebugTap::GTAOSelectedSurfaceOffset ||
				tap == PostProcessDebugTap::GTAODenoiseX ||
				tap == PostProcessDebugTap::GTAODenoiseY ||
				tap == PostProcessDebugTap::GTAOFinalAO ||
				tap == PostProcessDebugTap::GTAOAOOnlyLightingContribution ||
				tap == PostProcessDebugTap::GTAOHistorySamples;
		}

		bool IsTemporalMotionPreview(PostProcessDebugTap tap) noexcept
		{
			return tap == PostProcessDebugTap::TemporalMotionDirection ||
				tap == PostProcessDebugTap::TemporalMotionMagnitude;
		}

		bool IsTemporalAAPreview(PostProcessDebugTap tap) noexcept
		{
			return tap == PostProcessDebugTap::TemporalHistoryColor ||
				tap == PostProcessDebugTap::TemporalReprojectionUV ||
				tap == PostProcessDebugTap::TemporalRejection ||
				tap == PostProcessDebugTap::TemporalHistoryWeight ||
				tap == PostProcessDebugTap::TemporalHistorySamples ||
				tap == PostProcessDebugTap::TemporalClipDistance ||
				tap == PostProcessDebugTap::TemporalHistoryRelaxation;
		}

		// Records the fullscreen preview draw shared by inspector previews and
		// diagnostic captures.
		bool RecordPreviewDraw(RGExecuteContext& executeContext, const PassData& data,
			RHIPipelineHandle pipeline, const RenderServices& services,
			const RenderFrameContext& context, RenderViewID displayViewId) noexcept
		{
			auto* commandContext = executeContext.GetGraphicsCommandContext();
			const auto sourceSrv = executeContext.GetViewDescriptor(data.m_SourceSrv);
			const auto outputRtv = executeContext.GetViewHandle(data.m_OutputRtv);
			GGLAB_ASSERT_MSG(
				sourceSrv.IsValid(), "Post-process preview source SRV must be shader visible.");

			const RHIRenderingAttachment colorAttachment{
				.m_View = outputRtv,
				.m_LoadOp = RHIContentLoadOp::DontCare,
			};
			commandContext->BeginRendering({ .m_ColorAttachments =
				std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
			commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
			commandContext->SetPipeline(pipeline);
			commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width),
				static_cast<float>(data.m_Height) });
			commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width),
				static_cast<int32_t>(data.m_Height) });

			commandContext->SetConstantBuffer(
				static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
				context.m_FrameConstants.m_Scene);
			if (!commandContext->SetReadOnlyBuffer(
				static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
				context.m_FrameConstants.m_Views))
			{
				return false;
			}

			const PostProcessPreviewPassParameters parameters{
				.SourceTextureIndex = sourceSrv.m_Index,
				.SourceSamplerIndex = data.m_SamplerIndex,
				.ViewIndex = static_cast<uint32_t>(utils::ToIndex(displayViewId)),
				.SourceMode = static_cast<uint32_t>(data.m_Selection.m_Tap),
				.SourcePreExposure = data.m_SourcePreExposure,
				.PreviewExposureScale = data.m_PreviewExposureScale,
				.ValidityTextureIndex = data.m_ValiditySrv.IsValid() ?
					executeContext.GetViewDescriptor(data.m_ValiditySrv).m_Index : UINT32_MAX,
				.ActivityTextureIndex = data.m_ActivitySrv.IsValid() ?
					executeContext.GetViewDescriptor(data.m_ActivitySrv).m_Index : UINT32_MAX,
			};
			if (!commandContext->SetConstantBufferData(
				static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
			{
				return false;
			}
			commandContext->DrawFullscreenTriangle();
			return true;
		}

		struct DiagnosticSource
		{
			RGTextureId m_Texture{};
			std::optional<RHITextureViewDesc> m_ViewDesc;
			RGTextureId m_Validity{};
			RGTextureId m_Activity{};
			GIPreviewSnapshot m_GISnapshot{};
		};

		DiagnosticSource ResolveGIPreviewSource(const RGGIPreviewResources* gi,
			PostProcessDebugTap tap) noexcept
		{
			if (!gi)
			{
				return {};
			}
			DiagnosticSource source{};
			switch (tap)
			{
			case PostProcessDebugTap::GIIrradiance:
				source.m_Texture = gi->m_Irradiance;
				break;
			case PostProcessDebugTap::GILambertRadiance:
				source.m_Texture = gi->m_LambertRadiance;
				break;
			case PostProcessDebugTap::GISunIrradiance:
				source.m_Texture = gi->m_SunIrradiance;
				break;
			case PostProcessDebugTap::GIEnvironmentIrradiance:
				source.m_Texture = gi->m_EnvironmentIrradiance;
				break;
			case PostProcessDebugTap::GIBaseVisibility:
			case PostProcessDebugTap::GICoatVisibility:
			case PostProcessDebugTap::GICombinedVisibility:
				return { .m_Texture = gi->m_Visibility, .m_Validity = gi->m_Visibility,
					.m_Activity = gi->m_Activity, .m_GISnapshot = gi->m_VisibilitySnapshot };
			default:
				return {};
			}
			source.m_Validity = gi->m_Irradiance;
			source.m_GISnapshot = gi->m_DiffuseSnapshot;
			return source;
		}

		// An ambient-occlusion reference frame presents its running mean as the final AO.
		RGTextureId ResolveGTAOPreviewSource(const RGGTAOResources& resources,
			const RGTemporalReferenceResources* reference, PostProcessDebugTap tap) noexcept
		{
			if (tap == PostProcessDebugTap::GTAOFinalAO && reference &&
				reference->m_MeanAmbientOcclusion.IsValid())
			{
				return reference->m_MeanAmbientOcclusion;
			}
			switch (tap)
			{
			case PostProcessDebugTap::GTAORawAO:
				return resources.m_RawAO;
			case PostProcessDebugTap::GTAOHalfDepthViewZ:
				return resources.m_HalfDepthViewZ;
			case PostProcessDebugTap::GTAOReconstructedNormal:
				return resources.m_ReconstructedNormal;
			case PostProcessDebugTap::GTAOSelectedSurfaceOffset:
				return resources.m_SelectedSurfaceOffset;
			case PostProcessDebugTap::GTAODenoiseX:
				return resources.m_DenoiseX;
			case PostProcessDebugTap::GTAODenoiseY:
				return resources.m_DenoiseY;
			case PostProcessDebugTap::GTAOFinalAO:
				return resources.m_FinalAO;
			case PostProcessDebugTap::GTAOAOOnlyLightingContribution:
				return resources.m_AOOnlyLightingContribution;
			case PostProcessDebugTap::GTAOHistorySamples:
				return resources.m_TemporalSamples;
			default:
				return {};
			}
		}
	}

	bool RenderPassPostProcessPreview::AddDiagnosticCapturePass(RenderGraph& rg,
		const RenderFrameContext& context, const RenderServices& services,
		PostProcessDebugTap tap) noexcept
	{
		auto& blackboard = rg.GetBlackboard();
		DiagnosticSource source{};
		if (IsGIPreview(tap))
		{
			source = ResolveGIPreviewSource(
				blackboard.TryGet<RGGIPreviewResources>(GIPreviewResourcesName), tap);
		}
		else if (IsTemporalAAPreview(tap))
		{
			const auto* temporalAA =
				blackboard.TryGet<RGTemporalAAResources>(TemporalAAResourcesName);
			source.m_Texture =
				temporalAA ? ResolveTemporalAAPreviewSource(*temporalAA, tap) : RGTextureId{};
		}
		else if (IsTemporalMotionPreview(tap))
		{
			const auto* temporalGeometry = blackboard.TryGet<RGTemporalGeometryResources>(
				TemporalGeometryResourcesName);
			if (temporalGeometry && temporalGeometry->IsValid())
			{
				source = { temporalGeometry->m_MotionVectors, temporalGeometry->m_MotionSrvDesc };
			}
		}
		else if (IsGTAOPreview(tap))
		{
			const auto* gtao = blackboard.TryGet<RGGTAOResources>(GTAOResourcesName);
			source.m_Texture = gtao ?
				ResolveGTAOPreviewSource(*gtao,
					blackboard.TryGet<RGTemporalReferenceResources>(TemporalReferenceResourcesName), tap) :
				RGTextureId{};
		}
		else if (IsDepthPreview(tap))
		{
			const auto* sceneDepth =
				blackboard.TryGet<RGSceneDepthResources>(SceneDepthResourcesName);
			if (sceneDepth)
			{
				source = { sceneDepth->m_Texture, sceneDepth->m_SrvDesc };
			}
		}
		if (!source.m_Texture.IsValid())
		{
			return false;
		}

		EnsureInitialized(services);
		const RenderView& displayView = context.GetDisplayRenderView();
		// RenderGraph infers usage from accesses: the target is rendered here and
		// copied by the Diagnostic capture pass, which receives the inferred usage.
		const RHITextureDesc outputDesc{
			.m_Format = RHIFormat::R8G8B8A8Unorm,
			.m_Extent = { displayView.m_DisplayWidth, displayView.m_DisplayHeight, 1u },
		};
		RHITextureDesc captureDesc = outputDesc;
		captureDesc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::CopySource;
		const uint32_t samplerIndex = services.m_Samplers->GetSamplerIndex(SamplerPreset::PointClamp);
		const RenderViewID displayViewId = context.GetDisplayViewId();
		const auto* contextPtr = &context;
		rg.AddPass<PassData>("Capture.DiagnosticTap",
			[source, outputDesc, captureDesc, samplerIndex, tap](
				RenderGraph::RGBuilder& builder, PassData& data)
			{
				data.m_Source = builder.Read(source.m_Texture, RGTextureAccess::Sample);
				if (source.m_Validity.IsValid())
				{
					data.m_ValiditySrv = builder.CreateView<RHITextureViewType::ShaderResource>(
						builder.Read(source.m_Validity, RGTextureAccess::Sample));
				}
				if (source.m_Activity.IsValid())
				{
					data.m_ActivitySrv = builder.CreateView<RHITextureViewType::ShaderResource>(
						builder.Read(source.m_Activity, RGTextureAccess::Sample));
				}
				data.m_SourceSrv = source.m_ViewDesc ?
					builder.CreateView<RHITextureViewType::ShaderResource>(
						data.m_Source, *source.m_ViewDesc) :
					builder.CreateView<RHITextureViewType::ShaderResource>(data.m_Source);
				data.m_Output = builder.CreateTexture("Capture.DiagnosticTap", outputDesc);
				builder.WriteInPlace(data.m_Output, RGTextureAccess::RenderTarget);
				data.m_OutputRtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_Output);
				data.m_Selection = { .m_Tap = tap };
				data.m_Width = outputDesc.m_Extent.m_Width;
				data.m_Height = outputDesc.m_Extent.m_Height;
				data.m_SamplerIndex = samplerIndex;
				builder.GetBlackboard().GetOrCreate<RGDiagnosticCaptureResources>(
					DiagnosticCaptureResourcesName) = {
					.m_Texture = data.m_Output,
					.m_Desc = captureDesc,
				};
			},
			[this, services, contextPtr, displayViewId](
				RGExecuteContext& executeContext, PassData& data)
			{
				RecordPreviewDraw(executeContext, data, GetOrCreatePSO(services), services,
					*contextPtr, displayViewId);
			});
		return true;
	}

	void RenderPassPostProcessPreview::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		for (uint32_t index = 0; index < static_cast<uint32_t>(PostProcessPreviewChannel::Count); ++index)
		{
			const auto channel = static_cast<PostProcessPreviewChannel>(index);
			if (services.m_Resources->IsPostProcessPreviewRequested(channel) ||
				channel == PostProcessPreviewChannel::GlobalIllumination)
			{
				AddPassForChannel(rg, context, services, channel);
			}
		}
	}

	void RenderPassPostProcessPreview::AddPassForChannel(RenderGraph& rg,
		const RenderFrameContext& context, const RenderServices& services,
		PostProcessPreviewChannel channel) noexcept
	{
		auto* registry = services.m_Resources;
		GGLAB_ASSERT_NOT_NULL(registry);
		const auto selection = registry->GetPostProcessPreviewSelection(channel);
		if (IsGIPreview(selection.m_Tap))
		{
			const auto source = ResolveGIPreviewSource(
				rg.GetBlackboard().TryGet<RGGIPreviewResources>(GIPreviewResourcesName), selection.m_Tap);
			if (!source.m_Texture.IsValid())
			{
				registry->InvalidatePostProcessPreview(selection, channel);
				return;
			}
			registry->SetGIPreviewSourceAvailable();
			if (registry->IsPostProcessPreviewRequested(channel))
			{
				AddResolvedPass(rg, context, services, source.m_Texture, 1.0f, std::nullopt, selection, channel,
					source.m_Validity, source.m_Activity, source.m_GISnapshot);
			}
			else if (registry->HasPublishedPostProcessPreview(channel))
			{
				// Frozen images still participate in the GUI's graph access contract.
				const auto textureIndex = RenderResourceRegistry::GetPostProcessPreviewTextureIndex(channel);
				const auto handle = registry->GetTextureHandle(textureIndex);
				const auto desc = *registry->GetTextureDesc(textureIndex);
				rg.AddPass<PassData>("GI.Preview.Frozen",
					[handle, desc, channel](RenderGraph::RGBuilder& builder, PassData&)
					{
						const auto output = builder.ImportTexture("Preview.GlobalIllumination", handle, desc,
							ToRHIResourceState(RGTextureAccess::Sample, RHIStage::PixelShader),
							RGContentValidity::Defined);
						builder.GetBlackboard().GetOrCreate<RGPostProcessPreviewResources>(
							PostProcessPreviewResourcesName).m_Textures[static_cast<size_t>(channel)] = output;
					}, [](RGExecuteContext&, PassData&) {});
			}
			return;
		}
		if (selection.m_Tap == PostProcessDebugTap::AtmosphereAerialTransmittance ||
			selection.m_Tap == PostProcessDebugTap::AtmosphereAerialInScattering)
		{
			const auto* aerial = rg.GetBlackboard().TryGet<RGAerialPerspectiveResources>(
				AerialPerspectiveResourcesName);
			if (!aerial || aerial->m_DiagnosticTap != selection.m_Tap ||
				!aerial->m_Diagnostic.IsValid())
			{
				registry->InvalidatePostProcessPreview(selection, channel);
				return;
			}
			if (!registry->IsPostProcessPreviewRequested(channel))
			{
				return;
			}
			AddResolvedPass(rg, context, services, aerial->m_Diagnostic,
				selection.m_Tap == PostProcessDebugTap::AtmosphereAerialInScattering ?
					context.GetDisplayRenderView().m_ScenePreExposure :
					1.0f,
				std::nullopt, selection, channel);
			return;
		}
		if (selection.m_Tap >= PostProcessDebugTap::AtmosphereTransmittance && selection.m_Tap <= PostProcessDebugTap::AtmosphereSkyView)
		{
			const auto* atmosphere = rg.GetBlackboard().TryGet<RGAtmosphereResources>(AtmosphereResourcesName);
			if (!atmosphere)
			{
				registry->InvalidatePostProcessPreview(selection, channel);
				return;
			}
			const auto index = static_cast<uint32_t>(selection.m_Tap) - static_cast<uint32_t>(PostProcessDebugTap::AtmosphereTransmittance);
			if (!registry->IsPostProcessPreviewRequested(channel))
			{
				return;
			}
			AddResolvedPass(rg, context, services, atmosphere->m_Luts[index], 1.0f, std::nullopt, selection, channel);
			return;
		}
		if (IsTemporalAAPreview(selection.m_Tap))
		{
			const auto* temporalAA = rg.GetBlackboard().TryGet<RGTemporalAAResources>(
				TemporalAAResourcesName);
			const RGTextureId source = temporalAA ?
				ResolveTemporalAAPreviewSource(*temporalAA, selection.m_Tap) :
				RGTextureId{};
			if (!temporalAA || !source.IsValid())
			{
				registry->InvalidatePostProcessPreview(selection, channel);
				return;
			}
			if (!registry->IsPostProcessPreviewRequested(channel))
			{
				return;
			}
			AddResolvedPass(rg, context, services, source, 1.0f, std::nullopt, selection, channel);
			return;
		}
		if (IsTemporalMotionPreview(selection.m_Tap))
		{
			const auto* temporalGeometry = rg.GetBlackboard().TryGet<
				RGTemporalGeometryResources>(TemporalGeometryResourcesName);
			if (!temporalGeometry || !temporalGeometry->IsValid())
			{
				registry->InvalidatePostProcessPreview(selection, channel);
				return;
			}
			if (!registry->IsPostProcessPreviewRequested(channel))
			{
				return;
			}
			AddResolvedPass(rg, context, services, temporalGeometry->m_MotionVectors, 1.0f,
				temporalGeometry->m_MotionSrvDesc, selection, channel);
			return;
		}
		if (IsGTAOPreview(selection.m_Tap))
		{
			auto& blackboard = rg.GetBlackboard();
			const auto& gtaoResources = blackboard.Get<RGGTAOResources>(GTAOResourcesName);
			const RGTextureId source = ResolveGTAOPreviewSource(gtaoResources,
				blackboard.TryGet<RGTemporalReferenceResources>(TemporalReferenceResourcesName),
				selection.m_Tap);
			if (!source.IsValid())
			{
				registry->InvalidatePostProcessPreview(selection, channel);
				return;
			}
			if (!registry->IsPostProcessPreviewRequested(channel))
			{
				return;
			}
			AddResolvedPass(rg, context, services, source, 1.0f, std::nullopt, selection, channel);
			return;
		}
		if (IsDepthPreview(selection.m_Tap))
		{
			if (!registry->IsPostProcessPreviewRequested(channel))
			{
				return;
			}
			const auto& sceneDepth =
				rg.GetBlackboard().Get<RGSceneDepthResources>(SceneDepthResourcesName);
			AddResolvedPass(
				rg, context, services, sceneDepth.m_Texture, 1.0f, sceneDepth.m_SrvDesc, selection, channel);
			return;
		}
		if (selection.m_Tap != PostProcessDebugTap::SceneColor &&
			selection.m_Tap != PostProcessDebugTap::BloomResult)
		{
			return;
		}

		const auto& resources =
			rg.GetBlackboard().Get<RGPostProcessResources>(PostProcessResourcesName);
		const RGPostProcessColor source = ResolvePreviewSource(resources, selection);
		if (!source.m_Texture.IsValid())
		{
			registry->InvalidatePostProcessPreview(selection, channel);
			return;
		}
		GGLAB_ASSERT_MSG(source.m_State == PostProcessColorState::SceneLinearRec709,
			"Post-process preview requires scene-linear Rec.709 input.");
		if (registry->IsPostProcessPreviewRequested(channel))
		{
			AddResolvedPass(rg, context, services, source.m_Texture, source.m_PreExposure,
				std::nullopt, selection, channel);
		}
	}

	void RenderPassPostProcessPreview::AddPassForTap(RenderGraph& rg,
		const RenderFrameContext& context, const RenderServices& services,
		const RGPostProcessColor& source, PostProcessDebugTap tap,
		uint32_t bloomPyramidLevel) noexcept
	{
		auto* registry = services.m_Resources;
		GGLAB_ASSERT_NOT_NULL(registry);
		for (uint32_t index = 0; index < static_cast<uint32_t>(PostProcessPreviewChannel::Count); ++index)
		{
			const auto channel = static_cast<PostProcessPreviewChannel>(index);
			if (!registry->IsPostProcessPreviewRequested(channel))
			{
				continue;
			}
			const auto selection = registry->GetPostProcessPreviewSelection(channel);
			const bool levelMatches = tap != PostProcessDebugTap::BloomPyramid ||
				selection.m_BloomPyramidLevel == bloomPyramidLevel;
			if (selection.m_Tap != tap || !levelMatches || !source.m_Texture.IsValid())
			{
				continue;
			}
			GGLAB_ASSERT_MSG(source.m_State == PostProcessColorState::SceneLinearRec709,
				"Post-process preview requires scene-linear Rec.709 input.");
			if (!registry->IsPostProcessPreviewRequested(channel))
			{
				continue;
			}
			AddResolvedPass(rg, context, services, source.m_Texture, source.m_PreExposure,
				std::nullopt, selection, channel);
		}
	}

	void RenderPassPostProcessPreview::AddResolvedPass(RenderGraph& rg,
		const RenderFrameContext& context, const RenderServices& services, RGTextureId source,
		float sourcePreExposure, std::optional<RHITextureViewDesc> sourceViewDesc,
		PostProcessDebugSelection selection, PostProcessPreviewChannel channel,
		RGTextureId validity, RGTextureId activity, GIPreviewSnapshot snapshot) noexcept
	{
		auto* registry = services.m_Resources;
		GGLAB_ASSERT_NOT_NULL(registry);
		if (!source.IsValid())
		{
			return;
		}
		GGLAB_ASSERT_MSG(
			std::isfinite(sourcePreExposure) && sourcePreExposure > 0.0f,
			"Post-process preview requires positive finite pre-exposure.");

		EnsureInitialized(services);
		const RenderViewID displayViewId = context.GetDisplayViewId();
		const RenderView& displayView = context.GetDisplayRenderView();
		const RHIFencePoint retireFence = services.m_Presentation->GetLastSubmittedFencePoint();
		registry->EnsurePostProcessPreviewResources(
			displayView.m_DisplayWidth, displayView.m_DisplayHeight,
			retireFence.IsValid() ? &retireFence : nullptr, channel);

		auto& added = rg.GetBlackboard().GetOrCreate<PreviewBuildState>("PostProcess.Preview.Build")
			.m_Added[static_cast<std::size_t>(channel)];
		if (added)
		{
			return;
		}
		added = true;
		const auto request = registry->ObservePostProcessPreviewRequest(channel);

		using TextureIndex = RenderResourceRegistry::TextureIndex;
		const TextureIndex PreviewIndex = RenderResourceRegistry::GetPostProcessPreviewTextureIndex(channel);
		const auto* outputDesc = registry->GetTextureDesc(PreviewIndex);
		GGLAB_ASSERT_NOT_NULL(outputDesc);
		const RGPersistentTextureImportContract outputImport =
			ResolveRGPersistentTextureImportContract(
				registry->HasPublishedPostProcessPreview(channel) && !registry->IsDirty(PreviewIndex),
				ToRHIResourceState(RGTextureAccess::Sample, RHIStage::PixelShader));
		const bool pointSampledPreview =
			IsGIPreview(selection.m_Tap) || IsDepthPreview(selection.m_Tap) || IsGTAOPreview(selection.m_Tap) ||
			IsTemporalMotionPreview(selection.m_Tap) || IsTemporalAAPreview(selection.m_Tap);
		const uint32_t samplerIndex = services.m_Samplers->GetSamplerIndex(
			pointSampledPreview ? SamplerPreset::PointClamp : SamplerPreset::LinearClamp);
		const float previewExposureScale = std::exp2(registry->GetPostProcessPreviewExposureEV(channel));
		const auto* contextPtr = &context;

		static constexpr const char* PassNames[] =
		{
			"PostProcess.Preview", "AmbientOcclusion.Preview", "TemporalAA.Preview",
			"Atmosphere.Preview", "SceneDepth.Preview", "GI.Preview"
		};
		rg.AddPass<PassData>(
			PassNames[static_cast<uint32_t>(channel)],
			[source, sourcePreExposure, sourceViewDesc, selection, outputDesc, outputImport, samplerIndex, previewExposureScale, registry, PreviewIndex, channel, validity, activity]
			(RenderGraph::RGBuilder& builder, PassData& data)
			{
				data.m_Source = builder.Read(source, RGTextureAccess::Sample);
				if (validity.IsValid())
				{
					data.m_ValiditySrv = builder.CreateView<RHITextureViewType::ShaderResource>(
						builder.Read(validity, RGTextureAccess::Sample));
				}
				if (activity.IsValid())
				{
					data.m_ActivitySrv = builder.CreateView<RHITextureViewType::ShaderResource>(
						builder.Read(activity, RGTextureAccess::Sample));
				}
				if (selection.m_Tap == PostProcessDebugTap::SceneColor)
				{
					builder.GetBlackboard().GetOrCreate<NonFiniteWatchDefaultRegistry>().Register(
						NonFiniteWatchSemantic::PostProcessInput, data.m_Source,
						builder.GetObservationAddress(data.m_Source, RGObservationPosition::Before));
				}
				constexpr const char* PreviewNames[] =
				{
					"Preview.PostProcessing", "Preview.AmbientOcclusion", "Preview.TemporalAA",
					"Preview.Atmosphere", "Preview.SceneDepth", "Preview.GlobalIllumination"
				};
				data.m_Output = builder.ImportTexture(PreviewNames[static_cast<uint32_t>(channel)],
					registry->GetTextureHandle(PreviewIndex), *outputDesc,
					outputImport.m_InitialState, outputImport.m_InitialContentValidity);
				builder.WriteInPlace(data.m_Output, RGTextureAccess::RenderTarget);
				if (!sourceViewDesc)
				{
					data.m_SourceSrv =
						builder.CreateView<RHITextureViewType::ShaderResource>(data.m_Source);
				}
				else
				{
					data.m_SourceSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
						data.m_Source, *sourceViewDesc);
				}
				data.m_OutputRtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_Output);
				builder.GetBlackboard().GetOrCreate<RGPostProcessPreviewResources>(
					PostProcessPreviewResourcesName).m_Textures[static_cast<size_t>(channel)] = data.m_Output;
				data.m_Selection = selection;
				data.m_Width = static_cast<uint32_t>(outputDesc->m_Extent.m_Width);
				data.m_Height = outputDesc->m_Extent.m_Height;
				data.m_SamplerIndex = samplerIndex;
				data.m_SourcePreExposure = sourcePreExposure;
				data.m_PreviewExposureScale = previewExposureScale;
			},
			[this, services, registry, contextPtr, displayViewId, channel, request, snapshot](
				RGExecuteContext& executeContext, PassData& data)
			{
				if (!RecordPreviewDraw(executeContext, data, GetOrCreatePSO(services), services,
					*contextPtr, displayViewId))
				{
					return;
				}
				executeContext.DeferCommit(
					[registry, selection = data.m_Selection, channel, request, snapshot]
					(const RGCommitContext& commit) noexcept
					{
						registry->PublishPostProcessPreview(selection, channel, commit.m_FrameSerial, request);
						if (channel == PostProcessPreviewChannel::GlobalIllumination)
						{
							registry->PublishGIPreviewSnapshot(snapshot);
						}
					});
			});
	}

	void RenderPassPostProcessPreview::EnsureInitialized(const RenderServices& services) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}
		auto* shaderManager = services.m_ShaderPrograms;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		m_BaseRecipe.m_VSId =
			shaderManager->LoadProgram(shader_programs::PostProcessPreviewVertex);
		m_BaseRecipe.m_PSId =
			shaderManager->LoadProgram(shader_programs::PostProcessPreviewPixel);

		m_BaseRecipe.m_BindingLayout = services.m_BindingLayout->GetCommonBindingLayout();
		m_BaseRecipe.m_InputLayoutId = InputLayoutID::None;
		m_BaseRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		m_BaseRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		m_BaseRecipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R8G8B8A8Unorm;
		m_BaseRecipe.m_Formats.m_RenderTargetCount = 1;
		m_BaseRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::Unknown;
		m_BaseRecipe.m_Formats.m_SampleCount = 1;
		m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
		m_BaseRecipe.m_BlendPreset = BlendPreset::Default;
		m_BaseRecipe.m_DepthPreset = DepthPreset::DepthDisabled;
		m_IsInitialized = true;
	}

	RHIPipelineHandle RenderPassPostProcessPreview::GetOrCreatePSO(
		const RenderServices& services) noexcept
	{
		auto* pipelineCache = services.m_PipelineResolver;
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_BaseRecipe, GetInfo());
	}
}
