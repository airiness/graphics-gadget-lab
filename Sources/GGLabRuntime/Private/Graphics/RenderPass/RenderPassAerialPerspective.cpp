#include "Graphics/RenderPass/RenderPassAerialPerspective.h"
#include "Graphics/RenderPass/AerialPerspectiveGraphResources.h"
#include "Graphics/RenderPass/AtmosphereGraphResources.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gglab
{
	namespace
	{
		constexpr uint32_t AerialSliceCount = 32;
		constexpr uint32_t AerialThreadGroupSize = 8;

		struct AerialBuildParameters
		{
			uint32_t m_TransmittanceIndex = 0;
			uint32_t m_MultipleIndex = 0;
			uint32_t m_UnusedSource2 = 0;
			uint32_t m_UnusedSource3 = 0;
			uint32_t m_RadianceOutputIndex = 0;
			uint32_t m_ThroughputOutputIndex = 0;
			uint32_t m_SamplerIndex = 0;
			uint32_t m_ViewIndex = 0;
			uint32_t m_GridWidth = 0;
			uint32_t m_GridHeight = 0;
			uint32_t m_SliceCount = AerialSliceCount;
			float m_MaxDistanceKm = 0.0f;
			Vector3 m_SunDirection = Vector3::UnitY;
			uint32_t m_Padding = 0;
		};
		static_assert(sizeof(AerialBuildParameters) == 64 && IsPassRootConstantStruct<AerialBuildParameters>);

		struct AerialCompositeParameters
		{
			uint32_t m_SceneColorIndex = 0;
			uint32_t m_DepthIndex = 0;
			uint32_t m_RadianceIndex = 0;
			uint32_t m_ThroughputIndex = 0;
			uint32_t m_ColorOutputIndex = 0;
			uint32_t m_DiagnosticOutputIndex = 0;
			uint32_t m_SamplerIndex = 0;
			uint32_t m_ViewIndex = 0;
			uint32_t m_GridWidth = 0;
			uint32_t m_GridHeight = 0;
			uint32_t m_SliceCount = AerialSliceCount;
			float m_MaxDistanceKm = 0.0f;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_DiagnosticMode = 0;
			uint32_t m_Padding = 0;
		};
		static_assert(sizeof(AerialCompositeParameters) == 64 && IsPassRootConstantStruct<AerialCompositeParameters>);

		struct BuildPassData
		{
			RGTextureViewId m_Transmittance{};
			RGTextureViewId m_Multiple{};
			RGTextureViewId m_RadianceOutput{};
			RGTextureViewId m_ThroughputOutput{};
		};

		struct CompositePassData
		{
			RGTextureViewId m_SceneColor{};
			RGTextureViewId m_Depth{};
			RGTextureViewId m_Radiance{};
			RGTextureViewId m_Throughput{};
			RGTextureViewId m_ColorOutput{};
			RGTextureViewId m_DiagnosticOutput{};
		};
	}

	void RenderPassAerialPerspective::AddPass(RenderGraph& rg, const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		if (!context.IsRenderSceneReady() || !context.m_RenderScene.m_Atmosphere ||
			!context.m_RenderScene.m_WorldSun || !services.m_Atmosphere ||
			!services.m_Atmosphere->GetConstants().IsValid() || !services.m_Environment)
		{
			return;
		}
		const auto& environment = services.m_Environment->GetEnvironmentLightingSettings();
		const auto* atmosphereResources = rg.GetBlackboard().TryGet<RGAtmosphereResources>(
			AtmosphereResourcesName);
		if (!environment.m_EnableSkybox ||
			environment.m_BackgroundMode == EnvironmentBackgroundMode::TextureEnvironment ||
			!atmosphereResources)
		{
			return;
		}
		if (!m_BuildRecipe.m_CSId.IsValid())
		{
			m_BuildRecipe.m_CSId = services.m_ShaderPrograms->LoadProgram(
				shader_programs::AerialPerspectiveBuildCompute);
			m_CompositeRecipe.m_CSId = services.m_ShaderPrograms->LoadProgram(
				shader_programs::AerialPerspectiveCompositeCompute);
			m_BuildRecipe.m_BindingLayout = services.m_BindingLayout->GetCommonBindingLayout();
			m_CompositeRecipe.m_BindingLayout = m_BuildRecipe.m_BindingLayout;
		}
		const RHIPipelineHandle buildPipeline = services.m_PipelineResolver->Resolve(
			m_BuildSlot, m_BuildRecipe, GetInfo());
		const RHIPipelineHandle compositePipeline = services.m_PipelineResolver->Resolve(
			m_CompositeSlot, m_CompositeRecipe, GetInfo());
		if (!buildPipeline.IsValid() || !compositePipeline.IsValid()) return;

		const RenderViewID displayViewId = context.GetDisplayViewId();
		const auto& view = context.GetDisplayRenderView();
		const uint32_t gridWidth = std::min(240u, std::max(1u, (view.m_Width + 7u) / 8u));
		const uint32_t gridHeight = std::min(135u, std::max(1u, (view.m_Height + 7u) / 8u));
		const float halfFovTangent = std::tan(view.m_FovRadians * 0.5f);
		const float farCornerFactor = std::min(8.0f, std::sqrt(1.0f +
			halfFovTangent * halfFovTangent * (1.0f + view.m_Aspect * view.m_Aspect)));
		const float maxDistanceKm = std::min(300.0f,
			std::max(0.02f, view.m_Far *
				atmosphereResources->m_Diagnostics.m_Parameters.m_World.m_W * farCornerFactor));
		const uint32_t viewIndex = context.m_RenderScene.m_ViewBaseIndex +
			static_cast<uint32_t>(utils::ToIndex(displayViewId));
		const uint32_t samplerIndex = services.m_Samplers->GetSamplerIndex(SamplerPreset::LinearClamp);
		const Vector3 sunDirection = -context.m_RenderScene.m_WorldSun->m_Direction;
		const auto channel = PostProcessPreviewChannel::Atmosphere;
		const bool previewRequested = services.m_Resources->IsPostProcessPreviewRequested(channel);
		const PostProcessDebugTap selectedTap = previewRequested
			? services.m_Resources->GetPostProcessPreviewSelection(channel).m_Tap
			: PostProcessDebugTap::Count;
		const uint32_t diagnosticMode = selectedTap == PostProcessDebugTap::AtmosphereAerialTransmittance
			? 1u : selectedTap == PostProcessDebugTap::AtmosphereAerialInScattering ? 2u : 0u;
		auto& aerial = rg.GetBlackboard().Create<RGAerialPerspectiveResources>(
			AerialPerspectiveResourcesName);
		aerial.m_DiagnosticTap = diagnosticMode ? selectedTap : PostProcessDebugTap::Count;

		rg.AddPass<BuildPassData>("Atmosphere.AerialPerspective.Build", RGPassEncoderType::Compute,
			[gridWidth, gridHeight](RenderGraph::RGBuilder& builder, BuildPassData& data)
			{
				auto& blackboard = builder.GetBlackboard();
				auto& resources = blackboard.Get<RGAerialPerspectiveResources>(AerialPerspectiveResourcesName);
				const auto& atmosphere = blackboard.Get<RGAtmosphereResources>(AtmosphereResourcesName);
				RHITextureDesc atlasDesc{};
				atlasDesc.m_Format = RHIFormat::R32G32B32A32Float;
				atlasDesc.m_Extent = { gridWidth, gridHeight * AerialSliceCount, 1 };
				resources.m_RadianceAtlas = builder.CreateTexture("Atmosphere.AerialRadiance", atlasDesc);
				atlasDesc.m_Format = RHIFormat::R16G16B16A16Float;
				resources.m_ThroughputAtlas = builder.CreateTexture("Atmosphere.AerialThroughput", atlasDesc);
				data.m_Transmittance = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(atmosphere.m_Luts[0], RGTextureAccess::Sample, RHIStage::ComputeShader));
				data.m_Multiple = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(atmosphere.m_Luts[1], RGTextureAccess::Sample, RHIStage::ComputeShader));
				builder.WriteInPlace(resources.m_RadianceAtlas,
					RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				builder.WriteInPlace(resources.m_ThroughputAtlas,
					RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_RadianceOutput = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					resources.m_RadianceAtlas);
				data.m_ThroughputOutput = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					resources.m_ThroughputAtlas);
			},
			[services, buildPipeline, gridWidth, gridHeight, maxDistanceKm, viewIndex,
				samplerIndex, sunDirection](RGExecuteContext& execute, BuildPassData& data)
			{
				auto* command = execute.GetDirectComputeCommandContext();
				AerialBuildParameters parameters{};
				parameters.m_TransmittanceIndex = execute.GetViewDescriptor(data.m_Transmittance).m_Index;
				parameters.m_MultipleIndex = execute.GetViewDescriptor(data.m_Multiple).m_Index;
				parameters.m_RadianceOutputIndex = execute.GetViewDescriptor(data.m_RadianceOutput).m_Index;
				parameters.m_ThroughputOutputIndex = execute.GetViewDescriptor(data.m_ThroughputOutput).m_Index;
				parameters.m_SamplerIndex = samplerIndex;
				parameters.m_ViewIndex = viewIndex;
				parameters.m_GridWidth = gridWidth;
				parameters.m_GridHeight = gridHeight;
				parameters.m_MaxDistanceKm = maxDistanceKm;
				parameters.m_SunDirection = sunDirection;
				command->SetPipeline(buildPipeline);
				command->SetConstantBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					services.m_Atmosphere->GetConstants(), 0);
				command->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					services.m_FrameBuffers->GetViewStructuredBuffer()->GetBufferHandle());
				command->SetPushConstants(static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					parameters);
				command->Dispatch((gridWidth + AerialThreadGroupSize - 1) / AerialThreadGroupSize,
					(gridHeight + AerialThreadGroupSize - 1) / AerialThreadGroupSize, 1);
			});

		rg.AddPass<CompositePassData>("Atmosphere.AerialPerspective.Composite",
			RGPassEncoderType::Compute,
			[displayViewId, diagnosticMode, selectedTap](RenderGraph::RGBuilder& builder,
				CompositePassData& data)
			{
				auto& blackboard = builder.GetBlackboard();
				auto& resources = blackboard.Get<RGAerialPerspectiveResources>(AerialPerspectiveResourcesName);
				auto& targets = blackboard.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);
				const auto& sceneDepth = blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto color = builder.Read(targets.m_SceneColor, RGTextureAccess::Sample,
					RHIStage::ComputeShader);
				const auto depth = builder.Read(sceneDepth.m_Texture, RGTextureAccess::Sample,
					RHIStage::ComputeShader);
				const auto radiance = builder.Read(resources.m_RadianceAtlas,
					RGTextureAccess::Sample, RHIStage::ComputeShader);
				const auto throughput = builder.Read(resources.m_ThroughputAtlas,
					RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_SceneColor = builder.CreateView<RHITextureViewType::ShaderResource>(color);
				data.m_Depth = builder.CreateView<RHITextureViewType::ShaderResource>(
					depth, sceneDepth.m_SrvDesc);
				data.m_Radiance = builder.CreateView<RHITextureViewType::ShaderResource>(radiance);
				data.m_Throughput = builder.CreateView<RHITextureViewType::ShaderResource>(throughput);
				const auto& colorDesc = builder.GetTextureDesc(color);
				RHITextureDesc outputDesc{};
				outputDesc.m_Format = colorDesc.m_Format;
				outputDesc.m_Extent = colorDesc.m_Extent;
				auto output = builder.CreateTexture("Atmosphere.AerialSceneColor", outputDesc);
				builder.WriteInPlace(output, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_ColorOutput = builder.CreateView<RHITextureViewType::UnorderedAccess>(output);
				targets.m_SceneColor = output;
				if (diagnosticMode)
				{
					resources.m_DiagnosticTap = selectedTap;
					resources.m_Diagnostic = builder.CreateTexture("Atmosphere.AerialDiagnostic", outputDesc);
					builder.WriteInPlace(resources.m_Diagnostic,
						RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_DiagnosticOutput = builder.CreateView<RHITextureViewType::UnorderedAccess>(
						resources.m_Diagnostic);
				}
			},
			[services, &context, compositePipeline, gridWidth, gridHeight, maxDistanceKm,
				viewIndex, samplerIndex, diagnosticMode](RGExecuteContext& execute,
				CompositePassData& data)
			{
				auto* command = execute.GetDirectComputeCommandContext();
				AerialCompositeParameters parameters{};
				parameters.m_SceneColorIndex = execute.GetViewDescriptor(data.m_SceneColor).m_Index;
				parameters.m_DepthIndex = execute.GetViewDescriptor(data.m_Depth).m_Index;
				parameters.m_RadianceIndex = execute.GetViewDescriptor(data.m_Radiance).m_Index;
				parameters.m_ThroughputIndex = execute.GetViewDescriptor(data.m_Throughput).m_Index;
				parameters.m_ColorOutputIndex = execute.GetViewDescriptor(data.m_ColorOutput).m_Index;
				if (diagnosticMode)
				{
					parameters.m_DiagnosticOutputIndex = execute.GetViewDescriptor(data.m_DiagnosticOutput).m_Index;
				}
				parameters.m_SamplerIndex = samplerIndex;
				parameters.m_ViewIndex = viewIndex;
				parameters.m_GridWidth = gridWidth;
				parameters.m_GridHeight = gridHeight;
				parameters.m_MaxDistanceKm = maxDistanceKm;
				parameters.m_Width = context.GetDisplayRenderView().m_Width;
				parameters.m_Height = context.GetDisplayRenderView().m_Height;
				parameters.m_DiagnosticMode = diagnosticMode;
				command->SetPipeline(compositePipeline);
				command->SetConstantBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					services.m_Atmosphere->GetConstants(), 0);
				command->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					services.m_FrameBuffers->GetViewStructuredBuffer()->GetBufferHandle());
				command->SetPushConstants(static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					parameters);
				command->Dispatch((parameters.m_Width + AerialThreadGroupSize - 1) /
					AerialThreadGroupSize, (parameters.m_Height + AerialThreadGroupSize - 1) /
					AerialThreadGroupSize, 1);
			});
	}
}
