#include "Graphics/RenderPass/RenderPassGTAO.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Pipeline/GTAOCapability.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/RenderPass/TemporalGeometryGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		struct GTAOEvaluatePassParameters
		{
			uint32_t m_DepthTextureIndex = 0;
			uint32_t m_RawAOUavIndex = 0;
			uint32_t m_HalfDepthUavIndex = 0;
			uint32_t m_NormalUavIndex = 0;
			uint32_t m_SelectedOffsetUavIndex = 0;
			uint32_t m_ViewIndex = 0;
			uint32_t m_FullWidth = 0;
			uint32_t m_FullHeight = 0;
			uint32_t m_HalfWidth = 0;
			uint32_t m_HalfHeight = 0;
			uint32_t m_DirectionCount = 0;
			uint32_t m_StepCount = 0;
			float m_Radius = 0.0f;
			float m_FalloffStart = 0.0f;
			float m_FalloffEnd = 0.0f;
			uint32_t m_SampleIndex = 0;
		};
		static_assert(IsPassRootConstantStruct<GTAOEvaluatePassParameters>);
		static_assert(sizeof(GTAOEvaluatePassParameters) == 64);

		struct GTAOTemporalPassParameters
		{
			uint32_t m_RawAOIndex = 0;
			uint32_t m_HalfDepthIndex = 0;
			uint32_t m_FullDepthIndex = 0;
			uint32_t m_MotionIndex = 0;
			uint32_t m_PreviousVisibilityIndex = 0;
			uint32_t m_PreviousViewZIndex = 0;
			uint32_t m_NextVisibilityUavIndex = 0;
			uint32_t m_NextViewZUavIndex = 0;
			uint32_t m_AccumulatedAOUavIndex = 0;
			uint32_t m_ViewIndex = 0;
			uint32_t m_FullWidth = 0;
			uint32_t m_FullHeight = 0;
			uint32_t m_HalfWidth = 0;
			uint32_t m_HalfHeight = 0;
			uint32_t m_PreviousValid = 0;
			float m_MaxSamples = 1.0f;
		};
		static_assert(IsPassRootConstantStruct<GTAOTemporalPassParameters>);
		static_assert(sizeof(GTAOTemporalPassParameters) == 64);

		struct GTAODenoisePassParameters
		{
			uint32_t m_SourceAOIndex = 0;
			uint32_t m_HalfDepthIndex = 0;
			uint32_t m_OutputAOIndex = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_Radius = 0;
			uint32_t m_Padding0 = 0;
			uint32_t m_Padding1 = 0;
		};
		static_assert(IsPassRootConstantStruct<GTAODenoisePassParameters>);
		static_assert(sizeof(GTAODenoisePassParameters) == 32);

		struct GTAOUpsamplePassParameters
		{
			uint32_t m_DenoisedAOIndex = 0;
			uint32_t m_HalfDepthIndex = 0;
			uint32_t m_FullDepthIndex = 0;
			uint32_t m_FinalAOUavIndex = 0;
			uint32_t m_ViewIndex = 0;
			uint32_t m_FullWidth = 0;
			uint32_t m_FullHeight = 0;
			uint32_t m_HalfWidth = 0;
			uint32_t m_HalfHeight = 0;
			float m_Power = 1.0f;
			uint32_t m_Padding1 = 0;
			uint32_t m_Padding2 = 0;
		};
		static_assert(IsPassRootConstantStruct<GTAOUpsamplePassParameters>);
		static_assert(sizeof(GTAOUpsamplePassParameters) == 48);

		struct EvaluatePassData
		{
			RGTextureViewId m_DepthSrv{};
			RGTextureViewId m_RawAOUav{};
			RGTextureViewId m_HalfDepthUav{};
			RGTextureViewId m_NormalUav{};
			RGTextureViewId m_SelectedOffsetUav{};
			GTAOEvaluatePassParameters m_Parameters{};
			bool m_DiagnosticOutputsEnabled = false;
		};

		struct TemporalPassData
		{
			RGTextureViewId m_RawAOSrv{};
			RGTextureViewId m_HalfDepthSrv{};
			RGTextureViewId m_FullDepthSrv{};
			RGTextureViewId m_MotionSrv{};
			RGTextureViewId m_PreviousVisibilitySrv{};
			RGTextureViewId m_PreviousViewZSrv{};
			RGTextureViewId m_NextVisibilityUav{};
			RGTextureViewId m_NextViewZUav{};
			RGTextureViewId m_AccumulatedAOUav{};
			GTAOTemporalPassParameters m_Parameters{};
		};

		struct DenoisePassData
		{
			RGTextureViewId m_SourceAOSrv{};
			RGTextureViewId m_HalfDepthSrv{};
			RGTextureViewId m_OutputAOUav{};
			GTAODenoisePassParameters m_Parameters{};
		};

		struct UpsamplePassData
		{
			RGTextureViewId m_DenoisedAOSrv{};
			RGTextureViewId m_HalfDepthSrv{};
			RGTextureViewId m_FullDepthSrv{};
			RGTextureViewId m_FinalAOUav{};
			GTAOUpsamplePassParameters m_Parameters{};
		};

		void LogCapabilityFailure(
			std::string_view surfaceName, std::string_view requirement, RHIFormat format,
			RHITextureSupportResult result) noexcept
		{
			if (result.IsSupported())
			{
				return;
			}
			GGLAB_LOG_GRAPHICS_WARN(
				"GTAO surface '{}' cannot satisfy {} with {}: validation={}, support={}.",
				surfaceName, requirement, GetRHIFormatInfo(format).m_Name,
				RHITextureValidationErrorText(result.m_ValidationError),
				RHITextureSupportReasonText(result.m_Reason));
		}

		void LogSurfaceCapabilityFailures(std::string_view surfaceName, RHIFormat format,
			const GTAOSurfaceFormatSupport& support) noexcept
		{
			LogCapabilityFailure(
				surfaceName, "Texture2D shader-resource Load/Sample", format, support.m_ShaderResource);
			LogCapabilityFailure(surfaceName, "Texture2D typed UAV view/store", format,
				support.m_TypedUavStore);
		}

		bool RequiresGTAODiagnosticOutputs(PostProcessDebugTap tap) noexcept
		{
			return tap == PostProcessDebugTap::GTAOReconstructedNormal ||
				tap == PostProcessDebugTap::GTAOSelectedSurfaceOffset;
		}
	}

	void RenderPassGTAO::Prepare(const RenderServices& services) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}

		auto* shaderManager = services.m_ShaderPrograms;
		GGLAB_ASSERT_NOT_NULL(shaderManager);
		auto* device = services.m_Presentation->GetDevice();
		GGLAB_ASSERT_NOT_NULL(device);

		m_IsInitialized = true;
		m_Capabilities = QueryGTAOCapabilityStatus(*device);

		if (!m_Capabilities.m_R16Float.IsSupported())
		{
			LogSurfaceCapabilityFailures(
				"half-resolution AO", RHIFormat::R16Float, m_Capabilities.m_R16Float);
		}
		if (!m_Capabilities.m_R32Float.IsSupported())
		{
			LogSurfaceCapabilityFailures(
				"half-resolution view Z", RHIFormat::R32Float, m_Capabilities.m_R32Float);
		}
		if (!m_Capabilities.m_FinalAO.m_PreferredR8Unorm.IsSupported())
		{
			LogSurfaceCapabilityFailures("full-resolution AO preferred format", RHIFormat::R8Unorm,
				m_Capabilities.m_FinalAO.m_PreferredR8Unorm);
			if (m_Capabilities.m_FinalAO.UsesFallback())
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"GTAO full-resolution AO is falling back from R8Unorm to R16Float.");
			}
		}
		if (!m_Capabilities.IsCoreAvailable())
		{
			if (!m_Capabilities.m_FinalAO.IsAvailable())
			{
				LogSurfaceCapabilityFailures("full-resolution AO fallback", RHIFormat::R16Float,
					m_Capabilities.m_FinalAO.m_FallbackR16Float);
			}
			return;
		}

		const auto loadVariant = [services, shaderManager, this](
			PipelineVariant variant, const ShaderProgramRef& programRef) noexcept
			{
				auto& recipe = m_PipelineRecipes[static_cast<size_t>(variant)];
				recipe.m_CSId = shaderManager->LoadProgram(programRef);
				recipe.m_BindingLayout = services.m_BindingLayout->GetCommonBindingLayout();
				return recipe.m_CSId.IsValid() && recipe.m_BindingLayout.IsValid();
			};

		const bool evaluateReady =
			loadVariant(PipelineVariant::Evaluate, shader_programs::GTAOEvaluateCompute);
		m_DiagnosticPipelineAvailable = m_Capabilities.AreDiagnosticOutputsAvailable() &&
			loadVariant(PipelineVariant::EvaluateDiagnostics,
				shader_programs::GTAOEvaluateDiagnosticsCompute);
		m_TemporalPipelineAvailable = m_Capabilities.IsTemporalAvailable() &&
			loadVariant(PipelineVariant::Temporal, shader_programs::GTAOTemporalCompute);
		if (m_Capabilities.IsTemporalAvailable() && !m_TemporalPipelineAvailable)
		{
			// Frames that request temporal GTAO fall back to spatial-only visibility.
			GGLAB_LOG_GRAPHICS_ERROR("GTAO failed to prepare its temporal accumulation recipe.");
		}
		const bool denoiseXReady = loadVariant(PipelineVariant::DenoiseX,
			shader_programs::GTAODenoiseXCompute);
		const bool denoiseYReady = loadVariant(PipelineVariant::DenoiseY,
			shader_programs::GTAODenoiseYCompute);
		const bool upsampleReady = loadVariant(PipelineVariant::Upsample,
			shader_programs::GTAOUpsampleCompute);
		m_IsAvailable = evaluateReady && denoiseXReady && denoiseYReady && upsampleReady;
		if (!m_IsAvailable)
		{
			GGLAB_LOG_GRAPHICS_ERROR("GTAO failed to prepare one or more core pipeline recipes.");
		}
		if (!m_Capabilities.AreDiagnosticOutputsAvailable())
		{
			LogSurfaceCapabilityFailures("selected surface offset", RHIFormat::R16G16Float,
				m_Capabilities.m_R16G16Float);
			LogSurfaceCapabilityFailures("reconstructed normal", RHIFormat::R16G16B16A16Float,
				m_Capabilities.m_R16G16B16A16Float);
		}
	}

	void RenderPassGTAO::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "GTAO must be prepared before graph construction.");
		if (!m_IsAvailable)
		{
			// Frame validation fails an enabled, supported GTAO whose recipes failed to
			// prepare, so an unavailable pass here is either unsupported or disabled.
			GGLAB_ASSERT_MSG(!m_Capabilities.IsCoreAvailable() ||
				!context.GetDisplayViewRenderSettings().m_Lighting.m_GTAO.m_Enabled,
				"Enabled GTAO reached graph construction without prepared recipes.");
			return;
		}

		const auto& settings = context.GetDisplayViewRenderSettings().m_Lighting.m_GTAO;
		if (!settings.m_Enabled)
		{
			return;
		}

		auto* registry = services.m_Resources;
		GGLAB_ASSERT_NOT_NULL(registry);
		const uint32_t viewIndex =
			static_cast<uint32_t>(utils::ToIndex(context.GetDisplayViewId()));
		const bool diagnosticOutputsEnabled =
			registry->IsPostProcessPreviewRequested(PostProcessPreviewChannel::AmbientOcclusion) &&
			m_DiagnosticPipelineAvailable &&
			RequiresGTAODiagnosticOutputs(registry->GetPostProcessPreviewSelection(
				PostProcessPreviewChannel::AmbientOcclusion).m_Tap);
		const RHIFormat finalAOFormat =
			settings.m_FinalAOFormatPreference == GTAOFinalAOFormatPreference::ForceR16Float
			? RHIFormat::R16Float
			: m_Capabilities.m_FinalAO.m_Format;

		// Accumulating consumers advance the sampling sequence with every sample they
		// average: a supersampled reference with its sample index, temporal GTAO with the
		// submitted frame index. Every other frame keeps the fixed spatial pattern.
		TemporalFrameTransaction* transaction = context.m_TemporalFrameTransaction;
		const bool temporal = m_TemporalPipelineAvailable && transaction &&
			transaction->CanAccumulateAmbientOcclusion();
		uint32_t sampleIndex = 0;
		if (transaction && transaction->GetReferenceSample())
		{
			sampleIndex = transaction->GetReferenceSample()->m_Index;
		}
		else if (temporal)
		{
			sampleIndex = transaction->GetFrameIndex();
		}

		rg.AddPass<EvaluatePassData>(
			GetRenderGraphPassName(), RGPassEncoderType::Compute,
			[viewIndex, settings, diagnosticOutputsEnabled, capabilities = m_Capabilities,
			finalAOFormat, sampleIndex](
				RenderGraph::RGBuilder& builder, EvaluatePassData& data)
			{
				auto& blackboard = builder.GetBlackboard();
				const auto& sceneDepth =
					blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				GGLAB_ASSERT_MSG(sceneDepth.m_Convention == DepthConvention::Reversed,
					"GTAO currently requires Reversed-Z display depth.");

				const RHITextureDesc& depthDesc = builder.GetTextureDesc(sceneDepth.m_Texture);
				const GTAOExtent halfExtent = MakeGTAOHalfResolutionExtent(
					depthDesc.m_Extent.m_Width, depthDesc.m_Extent.m_Height);
				GGLAB_ASSERT_MSG(halfExtent.IsValid(), "GTAO requires a non-empty display extent.");

				RHITextureDesc outputDesc{};
				outputDesc.m_Extent = { halfExtent.m_Width, halfExtent.m_Height, 1 };
				auto& resources = blackboard.Get<RGGTAOResources>(GTAOResourcesName);
				resources.m_Capabilities = capabilities;
				resources.m_FinalAOFormat = finalAOFormat;
				resources.m_FullWidth = depthDesc.m_Extent.m_Width;
				resources.m_FullHeight = depthDesc.m_Extent.m_Height;
				resources.m_HalfWidth = halfExtent.m_Width;
				resources.m_HalfHeight = halfExtent.m_Height;
				outputDesc.m_Format = RHIFormat::R16Float;
				resources.m_RawAO = builder.CreateTexture("GTAO.RawAO", outputDesc);
				outputDesc.m_Format = RHIFormat::R32Float;
				resources.m_HalfDepthViewZ =
					builder.CreateTexture("GTAO.HalfDepthViewZ", outputDesc);
				if (diagnosticOutputsEnabled)
				{
					outputDesc.m_Format = RHIFormat::R16G16B16A16Float;
					resources.m_ReconstructedNormal =
						builder.CreateTexture("GTAO.ReconstructedNormal", outputDesc);
					outputDesc.m_Format = RHIFormat::R16G16Float;
					resources.m_SelectedSurfaceOffset =
						builder.CreateTexture("GTAO.SelectedSurfaceOffset", outputDesc);
				}

				const RGTextureId depth = builder.Read(
					sceneDepth.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_DepthSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					depth, sceneDepth.m_SrvDesc);
				builder.WriteInPlace(resources.m_RawAO, RGTextureAccess::StorageWrite,
					RHIStage::ComputeShader);
				builder.WriteInPlace(resources.m_HalfDepthViewZ, RGTextureAccess::StorageWrite,
					RHIStage::ComputeShader);
				data.m_RawAOUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					resources.m_RawAO);
				data.m_HalfDepthUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					resources.m_HalfDepthViewZ);
				if (diagnosticOutputsEnabled)
				{
					builder.WriteInPlace(resources.m_ReconstructedNormal,
						RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					builder.WriteInPlace(resources.m_SelectedSurfaceOffset,
						RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_NormalUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
						resources.m_ReconstructedNormal);
					data.m_SelectedOffsetUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
						resources.m_SelectedSurfaceOffset);
				}

				data.m_DiagnosticOutputsEnabled = diagnosticOutputsEnabled;
				data.m_Parameters = {
					.m_ViewIndex = viewIndex,
					.m_FullWidth = resources.m_FullWidth,
					.m_FullHeight = resources.m_FullHeight,
					.m_HalfWidth = resources.m_HalfWidth,
					.m_HalfHeight = resources.m_HalfHeight,
					.m_DirectionCount = settings.m_DirectionCount,
					.m_StepCount = settings.m_StepCount,
					.m_Radius = settings.m_Radius,
					.m_FalloffStart = settings.m_FalloffStart,
					.m_FalloffEnd = settings.m_FalloffEnd,
					.m_SampleIndex = sampleIndex,
				};
			},
			[this, services, &context](RGExecuteContext& executeContext, EvaluatePassData& data)
			{
				auto* commandContext = executeContext.GetDirectComputeCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				const auto depthSrv = executeContext.GetViewDescriptor(data.m_DepthSrv);
				const auto rawAOUav = executeContext.GetViewDescriptor(data.m_RawAOUav);
				const auto halfDepthUav = executeContext.GetViewDescriptor(data.m_HalfDepthUav);
				GGLAB_ASSERT_MSG(depthSrv.IsValid() && rawAOUav.IsValid() && halfDepthUav.IsValid(),
					"GTAO evaluate core views must be shader visible before dispatch.");

				auto parameters = data.m_Parameters;
				parameters.m_DepthTextureIndex = depthSrv.m_Index;
				parameters.m_RawAOUavIndex = rawAOUav.m_Index;
				parameters.m_HalfDepthUavIndex = halfDepthUav.m_Index;
				if (data.m_DiagnosticOutputsEnabled)
				{
					const auto normalUav = executeContext.GetViewDescriptor(data.m_NormalUav);
					const auto selectedOffsetUav =
						executeContext.GetViewDescriptor(data.m_SelectedOffsetUav);
					GGLAB_ASSERT_MSG(normalUav.IsValid() && selectedOffsetUav.IsValid(),
						"GTAO diagnostic views must be shader visible before dispatch.");
					parameters.m_NormalUavIndex = normalUav.m_Index;
					parameters.m_SelectedOffsetUavIndex = selectedOffsetUav.m_Index;
				}
				commandContext->SetPipeline(GetOrCreatePipeline(services,
					data.m_DiagnosticOutputsEnabled ? PipelineVariant::EvaluateDiagnostics
					: PipelineVariant::Evaluate));
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					services.m_FrameBuffers->GetSceneConstantBuffer()->GetBufferHandle(),
					context.m_RenderScene.m_SceneConstantBufferOffset);
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					services.m_FrameBuffers->GetViewStructuredBuffer()->GetBufferHandle());
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
				commandContext->Dispatch(
					(parameters.m_HalfWidth + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize,
					(parameters.m_HalfHeight + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize, 1);
			});

		if (temporal)
		{
			AddTemporalPass(rg, context, services, *transaction, viewIndex, settings);
		}

		const auto addDenoisePass = [this, &rg, settings, services](const char* passName,
			PipelineVariant variant, bool horizontal) noexcept
			{
				rg.AddPass<DenoisePassData>(
					passName, RGPassEncoderType::Compute,
					[settings, horizontal](RenderGraph::RGBuilder& builder, DenoisePassData& data)
					{
						auto& resources =
							builder.GetBlackboard().Get<RGGTAOResources>(GTAOResourcesName);
						const RGTextureId horizontalSource = resources.m_TemporalAO.IsValid()
							? resources.m_TemporalAO : resources.m_RawAO;
						const RGTextureId sourceAO = builder.Read(horizontal ? horizontalSource
							: resources.m_DenoiseX, RGTextureAccess::Sample, RHIStage::ComputeShader);
						const RGTextureId halfDepth = builder.Read(resources.m_HalfDepthViewZ,
							RGTextureAccess::Sample, RHIStage::ComputeShader);
						RHITextureDesc outputDesc{};
						outputDesc.m_Format = RHIFormat::R16Float;
						outputDesc.m_Extent = { resources.m_HalfWidth, resources.m_HalfHeight, 1 };
						auto& output = horizontal ? resources.m_DenoiseX : resources.m_DenoiseY;
						output = builder.CreateTexture(
							horizontal ? "GTAO.DenoiseX" : "GTAO.DenoiseY", outputDesc);
						builder.WriteInPlace(
							output, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
						data.m_SourceAOSrv =
							builder.CreateView<RHITextureViewType::ShaderResource>(sourceAO);
						data.m_HalfDepthSrv =
							builder.CreateView<RHITextureViewType::ShaderResource>(halfDepth);
						data.m_OutputAOUav =
							builder.CreateView<RHITextureViewType::UnorderedAccess>(output);
						data.m_Parameters = {
							.m_Width = resources.m_HalfWidth,
							.m_Height = resources.m_HalfHeight,
							.m_Radius = settings.m_DenoiseRadius,
						};
					},
					[this, services, variant](RGExecuteContext& executeContext, DenoisePassData& data)
					{
						auto* commandContext = executeContext.GetDirectComputeCommandContext();
						GGLAB_ASSERT_NOT_NULL(commandContext);
						const auto sourceAO = executeContext.GetViewDescriptor(data.m_SourceAOSrv);
						const auto halfDepth = executeContext.GetViewDescriptor(data.m_HalfDepthSrv);
						const auto outputAO = executeContext.GetViewDescriptor(data.m_OutputAOUav);
						GGLAB_ASSERT_MSG(sourceAO.IsValid() && halfDepth.IsValid() && outputAO.IsValid(),
							"GTAO denoise views must be shader visible before dispatch.");
						auto parameters = data.m_Parameters;
						parameters.m_SourceAOIndex = sourceAO.m_Index;
						parameters.m_HalfDepthIndex = halfDepth.m_Index;
						parameters.m_OutputAOIndex = outputAO.m_Index;
						commandContext->SetPipeline(GetOrCreatePipeline(services, variant));
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
						commandContext->Dispatch(
							(parameters.m_Width + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize,
							(parameters.m_Height + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize, 1);
					});
			};
		addDenoisePass("Lighting.GTAO.DenoiseX", PipelineVariant::DenoiseX, true);
		addDenoisePass("Lighting.GTAO.DenoiseY", PipelineVariant::DenoiseY, false);

		rg.AddPass<UpsamplePassData>(
			"Lighting.GTAO.Upsample", RGPassEncoderType::Compute,
			[viewIndex, finalAOFormat, settings](
				RenderGraph::RGBuilder& builder, UpsamplePassData& data)
			{
				auto& blackboard = builder.GetBlackboard();
				auto& resources = blackboard.Get<RGGTAOResources>(GTAOResourcesName);
				const auto& sceneDepth =
					blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const RGTextureId denoisedAO = builder.Read(
					resources.m_DenoiseY, RGTextureAccess::Sample, RHIStage::ComputeShader);
				const RGTextureId halfDepth = builder.Read(resources.m_HalfDepthViewZ,
					RGTextureAccess::Sample, RHIStage::ComputeShader);
				const RGTextureId fullDepth = builder.Read(
					sceneDepth.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);

				RHITextureDesc outputDesc{};
				outputDesc.m_Format = finalAOFormat;
				outputDesc.m_Extent = { resources.m_FullWidth, resources.m_FullHeight, 1 };
				resources.m_FinalAO = builder.CreateTexture("GTAO.FinalAO", outputDesc);
				builder.WriteInPlace(
					resources.m_FinalAO, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_DenoisedAOSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(denoisedAO);
				data.m_HalfDepthSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(halfDepth);
				data.m_FullDepthSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					fullDepth, sceneDepth.m_SrvDesc);
				data.m_FinalAOUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					resources.m_FinalAO);
				data.m_Parameters = {
					.m_ViewIndex = viewIndex,
					.m_FullWidth = resources.m_FullWidth,
					.m_FullHeight = resources.m_FullHeight,
					.m_HalfWidth = resources.m_HalfWidth,
					.m_HalfHeight = resources.m_HalfHeight,
					.m_Power = settings.m_Power,
				};
			},
			[this, services, &context](RGExecuteContext& executeContext, UpsamplePassData& data)
			{
				auto* commandContext = executeContext.GetDirectComputeCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				const auto denoisedAO = executeContext.GetViewDescriptor(data.m_DenoisedAOSrv);
				const auto halfDepth = executeContext.GetViewDescriptor(data.m_HalfDepthSrv);
				const auto fullDepth = executeContext.GetViewDescriptor(data.m_FullDepthSrv);
				const auto finalAO = executeContext.GetViewDescriptor(data.m_FinalAOUav);
				GGLAB_ASSERT_MSG(denoisedAO.IsValid() && halfDepth.IsValid() &&
					fullDepth.IsValid() && finalAO.IsValid(),
					"GTAO upsample views must be shader visible before dispatch.");
				auto parameters = data.m_Parameters;
				parameters.m_DenoisedAOIndex = denoisedAO.m_Index;
				parameters.m_HalfDepthIndex = halfDepth.m_Index;
				parameters.m_FullDepthIndex = fullDepth.m_Index;
				parameters.m_FinalAOUavIndex = finalAO.m_Index;
				commandContext->SetPipeline(GetOrCreatePipeline(services, PipelineVariant::Upsample));
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					services.m_FrameBuffers->GetSceneConstantBuffer()->GetBufferHandle(),
					context.m_RenderScene.m_SceneConstantBufferOffset);
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					services.m_FrameBuffers->GetViewStructuredBuffer()->GetBufferHandle());
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
				commandContext->Dispatch(
					(parameters.m_FullWidth + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize,
					(parameters.m_FullHeight + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize, 1);
			});
	}

	void RenderPassGTAO::AddTemporalPass(RenderGraph& rg, const RenderFrameContext& context,
		const RenderServices& services, TemporalFrameTransaction& transaction, uint32_t viewIndex,
		const GTAOSettings& settings) noexcept
	{
		rg.AddPass<TemporalPassData>(
			"Lighting.GTAO.Temporal", RGPassEncoderType::Compute,
			[&transaction, viewIndex, settings](
				RenderGraph::RGBuilder& builder, TemporalPassData& data)
			{
				auto& blackboard = builder.GetBlackboard();
				auto& resources = blackboard.Get<RGGTAOResources>(GTAOResourcesName);
				const auto& sceneDepth =
					blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto& geometry = blackboard.Get<RGTemporalGeometryResources>(
					TemporalGeometryResourcesName);
				GGLAB_ASSERT_MSG(resources.IsEvaluateValid() && geometry.IsValid(),
					"Temporal GTAO requires evaluated visibility and raster motion.");

				GTAOTemporalHistoryRenderGraphResources history{};
				const bool imported = transaction.ImportAmbientOcclusionHistory(builder, history);
				GGLAB_ASSERT_MSG(imported && history.IsValid(),
					"An active temporal GTAO frame must import its history.");

				const auto readSampled = [&builder](RGTextureId texture) noexcept
					{
						return builder.Read(
							texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
					};
				data.m_RawAOSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					readSampled(resources.m_RawAO));
				data.m_HalfDepthSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					readSampled(resources.m_HalfDepthViewZ));
				data.m_FullDepthSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					readSampled(sceneDepth.m_Texture), sceneDepth.m_SrvDesc);
				data.m_MotionSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					readSampled(geometry.m_MotionVectors), geometry.m_MotionSrvDesc);
				if (history.m_PreviousValid)
				{
					data.m_PreviousVisibilitySrv =
						builder.CreateView<RHITextureViewType::ShaderResource>(
							readSampled(history.m_PreviousVisibility));
					data.m_PreviousViewZSrv =
						builder.CreateView<RHITextureViewType::ShaderResource>(
							readSampled(history.m_PreviousViewZ));
				}

				RHITextureDesc accumulatedDesc{};
				accumulatedDesc.m_Format = RHIFormat::R16Float;
				accumulatedDesc.m_Extent = { resources.m_HalfWidth, resources.m_HalfHeight, 1 };
				resources.m_TemporalAO = builder.CreateTexture("GTAO.TemporalAO", accumulatedDesc);
				builder.WriteInPlace(history.m_NextVisibility, RGTextureAccess::StorageWrite,
					RHIStage::ComputeShader);
				builder.WriteInPlace(history.m_NextViewZ, RGTextureAccess::StorageWrite,
					RHIStage::ComputeShader);
				builder.WriteInPlace(resources.m_TemporalAO, RGTextureAccess::StorageWrite,
					RHIStage::ComputeShader);
				data.m_NextVisibilityUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					history.m_NextVisibility);
				data.m_NextViewZUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					history.m_NextViewZ);
				data.m_AccumulatedAOUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					resources.m_TemporalAO);
				const bool exported = transaction.ExportAmbientOcclusionHistory(builder, history);
				GGLAB_ASSERT_MSG(exported,
					"Temporal GTAO must export the history its pass fully writes.");
				GGLAB_UNUSED(imported);
				GGLAB_UNUSED(exported);

				data.m_Parameters = {
					.m_ViewIndex = viewIndex,
					.m_FullWidth = resources.m_FullWidth,
					.m_FullHeight = resources.m_FullHeight,
					.m_HalfWidth = resources.m_HalfWidth,
					.m_HalfHeight = resources.m_HalfHeight,
					.m_PreviousValid = history.m_PreviousValid ? 1u : 0u,
					.m_MaxSamples = static_cast<float>(settings.m_TemporalMaxSamples),
				};
			},
			[this, services, &context](RGExecuteContext& executeContext, TemporalPassData& data)
			{
				auto* commandContext = executeContext.GetDirectComputeCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				auto parameters = data.m_Parameters;
				const auto descriptorIndex = [&executeContext](RGTextureViewId view) noexcept
					{
						const auto descriptor = executeContext.GetViewDescriptor(view);
						GGLAB_ASSERT_MSG(descriptor.IsValid(),
							"Temporal GTAO views must be shader visible before dispatch.");
						return descriptor.m_Index;
					};
				parameters.m_RawAOIndex = descriptorIndex(data.m_RawAOSrv);
				parameters.m_HalfDepthIndex = descriptorIndex(data.m_HalfDepthSrv);
				parameters.m_FullDepthIndex = descriptorIndex(data.m_FullDepthSrv);
				parameters.m_MotionIndex = descriptorIndex(data.m_MotionSrv);
				if (parameters.m_PreviousValid != 0u)
				{
					parameters.m_PreviousVisibilityIndex =
						descriptorIndex(data.m_PreviousVisibilitySrv);
					parameters.m_PreviousViewZIndex = descriptorIndex(data.m_PreviousViewZSrv);
				}
				parameters.m_NextVisibilityUavIndex = descriptorIndex(data.m_NextVisibilityUav);
				parameters.m_NextViewZUavIndex = descriptorIndex(data.m_NextViewZUav);
				parameters.m_AccumulatedAOUavIndex = descriptorIndex(data.m_AccumulatedAOUav);
				commandContext->SetPipeline(GetOrCreatePipeline(services, PipelineVariant::Temporal));
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					services.m_FrameBuffers->GetSceneConstantBuffer()->GetBufferHandle(),
					context.m_RenderScene.m_SceneConstantBufferOffset);
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					services.m_FrameBuffers->GetViewStructuredBuffer()->GetBufferHandle());
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
				commandContext->Dispatch(
					(parameters.m_HalfWidth + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize,
					(parameters.m_HalfHeight + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize, 1);
			});
	}

	RHIPipelineHandle RenderPassGTAO::GetOrCreatePipeline(
		const RenderServices& services, PipelineVariant variant) noexcept
	{
		auto* pipelineCache = services.m_PipelineResolver;
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		const size_t index = static_cast<size_t>(variant);
		GGLAB_ASSERT(index < m_PipelineRecipes.size());
		const RHIPipelineHandle pipeline =
			pipelineCache->Resolve(m_PipelineSlots[index], m_PipelineRecipes[index], GetInfo());
		GGLAB_ASSERT_MSG(pipeline.IsValid(), "GTAO pipeline resolution returned an invalid handle.");
		return pipeline;
	}
}
