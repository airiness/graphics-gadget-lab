#include "Graphics/RenderPass/RenderPassGTAO.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/PostProcess/PostProcessDebug.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"

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
			float m_Thickness = 0.0f;
		};
		static_assert(IsPassRootConstantStruct<GTAOEvaluatePassParameters>);
		static_assert(sizeof(GTAOEvaluatePassParameters) == 64);

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

		GTAOSurfaceFormatSupport QueryGTAOSurfaceFormatSupport(
			RHIDevice& device, RHIFormat format) noexcept
		{
			const RHITextureDesc textureDesc{
				.m_Dimension = RHITextureDimension::Texture2D,
				.m_Format = format,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
				.m_Extent = { 1, 1, 1 },
			};
			auto viewDesc = MakeRHITexture2DViewDesc(format);
			viewDesc.m_Type = RHITextureViewType::ShaderResource;
			const RHITextureSupportResult shaderResource =
				device.QueryTextureViewSupport(textureDesc, viewDesc);
			viewDesc.m_Type = RHITextureViewType::UnorderedAccess;
			return {
				.m_ShaderResource = shaderResource,
				.m_TypedUavStore = device.QueryTextureViewSupport(textureDesc, viewDesc),
			};
		}

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

		auto* renderer = services.m_Renderer;
		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(renderer);
		GGLAB_ASSERT_NOT_NULL(shaderManager);
		auto* device = renderer->GetDevice();
		GGLAB_ASSERT_NOT_NULL(device);

		m_IsInitialized = true;
		m_Capabilities.m_R16Float =
			QueryGTAOSurfaceFormatSupport(*device, RHIFormat::R16Float);
		m_Capabilities.m_R32Float =
			QueryGTAOSurfaceFormatSupport(*device, RHIFormat::R32Float);
		m_Capabilities.m_R16G16Float =
			QueryGTAOSurfaceFormatSupport(*device, RHIFormat::R16G16Float);
		m_Capabilities.m_R16G16B16A16Float =
			QueryGTAOSurfaceFormatSupport(*device, RHIFormat::R16G16B16A16Float);
		m_Capabilities.m_FinalAO = ResolveGTAOFinalAOFormat(
			QueryGTAOSurfaceFormatSupport(*device, RHIFormat::R8Unorm),
			m_Capabilities.m_R16Float);

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

		const auto loadVariant = [renderer, shaderManager, this](
			PipelineVariant variant, const ShaderProgramRef& programRef) noexcept
			{
				auto& recipe = m_PipelineRecipes[static_cast<size_t>(variant)];
				recipe.m_CSId = shaderManager->LoadProgram(programRef);
				recipe.m_BindingLayout = renderer->GetCommonBindingLayout();
				return recipe.m_CSId.IsValid() && recipe.m_BindingLayout.IsValid();
			};

		const bool evaluateReady =
			loadVariant(PipelineVariant::Evaluate, shader_programs::GTAOEvaluateCompute);
		m_DiagnosticPipelineAvailable = m_Capabilities.AreDiagnosticOutputsAvailable() &&
			loadVariant(PipelineVariant::EvaluateDiagnostics,
				shader_programs::GTAOEvaluateDiagnosticsCompute);
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
			return;
		}

		const auto& settings = context.GetDisplayViewRenderSettings().m_Lighting.m_GTAO;
		if (!settings.m_Enabled)
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		auto* registry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(registry);
		const uint32_t viewIndex =
			static_cast<uint32_t>(utils::ToIndex(context.GetDisplayViewId()));
		const bool diagnosticOutputsEnabled = registry->IsPostProcessPreviewRequested() &&
			m_DiagnosticPipelineAvailable &&
			RequiresGTAODiagnosticOutputs(registry->GetPostProcessPreviewSelection().m_Tap);
		const RHIFormat finalAOFormat =
			settings.m_FinalAOFormatPreference == GTAOFinalAOFormatPreference::ForceR16Float
			? RHIFormat::R16Float
			: m_Capabilities.m_FinalAO.m_Format;

		rg.AddPass<EvaluatePassData>(
			GetRenderGraphPassName(), RGPassEncoderType::Compute,
			[viewIndex, settings, diagnosticOutputsEnabled, capabilities = m_Capabilities,
			finalAOFormat](
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
					.m_Thickness = settings.m_Thickness,
				};
			},
			[this, renderer, &context](RGExecuteContext& executeContext, EvaluatePassData& data)
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
				commandContext->SetPipeline(GetOrCreatePipeline(*renderer,
					data.m_DiagnosticOutputsEnabled ? PipelineVariant::EvaluateDiagnostics
					: PipelineVariant::Evaluate));
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					renderer->GetSceneConstantBuffer()->GetBufferHandle(),
					context.m_RenderScene.m_SceneConstantBufferOffset);
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					renderer->GetViewStructuredBuffer()->GetBufferHandle());
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
				commandContext->Dispatch(
					(parameters.m_HalfWidth + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize,
					(parameters.m_HalfHeight + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize, 1);
			});

		const auto addDenoisePass = [this, &rg, settings, renderer](const char* passName,
			PipelineVariant variant, bool horizontal) noexcept
			{
				rg.AddPass<DenoisePassData>(
					passName, RGPassEncoderType::Compute,
					[settings, horizontal](RenderGraph::RGBuilder& builder, DenoisePassData& data)
					{
						auto& resources =
							builder.GetBlackboard().Get<RGGTAOResources>(GTAOResourcesName);
						const RGTextureId sourceAO = builder.Read(horizontal ? resources.m_RawAO
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
					[this, renderer, variant](RGExecuteContext& executeContext, DenoisePassData& data)
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
						commandContext->SetPipeline(GetOrCreatePipeline(*renderer, variant));
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
			[this, renderer, &context](RGExecuteContext& executeContext, UpsamplePassData& data)
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
				commandContext->SetPipeline(GetOrCreatePipeline(*renderer, PipelineVariant::Upsample));
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					renderer->GetSceneConstantBuffer()->GetBufferHandle(),
					context.m_RenderScene.m_SceneConstantBufferOffset);
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					renderer->GetViewStructuredBuffer()->GetBufferHandle());
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
				commandContext->Dispatch(
					(parameters.m_FullWidth + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize,
					(parameters.m_FullHeight + GTAOThreadGroupSize - 1) / GTAOThreadGroupSize, 1);
			});
	}

	RHIPipelineHandle RenderPassGTAO::GetOrCreatePipeline(
		const Renderer& renderer, PipelineVariant variant) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		const size_t index = static_cast<size_t>(variant);
		GGLAB_ASSERT(index < m_PipelineRecipes.size());
		const RHIPipelineHandle pipeline =
			pipelineCache->Resolve(m_PipelineSlots[index], m_PipelineRecipes[index], GetInfo());
		GGLAB_ASSERT_MSG(pipeline.IsValid(), "GTAO pipeline resolution returned an invalid handle.");
		return pipeline;
	}
}
