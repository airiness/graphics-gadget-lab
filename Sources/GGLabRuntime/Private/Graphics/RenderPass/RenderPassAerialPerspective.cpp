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
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
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

		struct ProbeParameters
		{
			uint32_t m_SurfaceIndex = 0;
			uint32_t m_CompositeIndex = 0;
			uint32_t m_DepthIndex = 0;
			uint32_t m_RadianceIndex = 0;
			uint32_t m_ThroughputIndex = 0;
			uint32_t m_SamplerIndex = 0;
			uint32_t m_ViewIndex = 0;
			uint32_t m_GridWidth = 0;
			uint32_t m_GridHeight = 0;
			uint32_t m_SliceCount = AerialSliceCount;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			float m_MaxDistanceKm = 0.0f;
			float m_WorldScaleKm = 0.0f;
			uint32_t m_FrameSerialLow = 0;
			uint32_t m_FrameSerialHigh = 0;
		};
		static_assert(sizeof(ProbeParameters) == 64 && IsPassRootConstantStruct<ProbeParameters>);
		static_assert(offsetof(ProbeParameters, m_FrameSerialLow) == 56);
		static_assert(offsetof(ProbeParameters, m_FrameSerialHigh) == 60);

		struct ProbePassData
		{
			RGTextureViewId m_Surface{};
			RGTextureViewId m_Composite{};
			RGTextureViewId m_Depth{};
			RGTextureViewId m_Radiance{};
			RGTextureViewId m_Throughput{};
			RGBufferId m_Output{};
		};

		struct ProbeReadbackPassData
		{
			RGBufferId m_Source{};
			RGBufferId m_Destination{};
		};

		RHIBindingLayoutDesc BuildProbeBindingLayout() noexcept
		{
			RHIBindingLayoutDesc desc{};
			desc.m_DebugName = "Atmosphere.AerialProbeBindingLayout";
			desc.m_Slots[desc.m_SlotCount++] = { RHIBindingType::PushConstants,
				RHIShaderStage::Compute, 0, 0, 1, sizeof(ProbeParameters), "ProbeConstants" };
			desc.m_Slots[desc.m_SlotCount++] = { RHIBindingType::ReadWriteStorageBuffer,
				RHIShaderStage::Compute, 0, 0, 1, 0, "ProbeOutput" };
			desc.m_Slots[desc.m_SlotCount++] = { RHIBindingType::ReadOnlyStorageBuffer,
				RHIShaderStage::Compute, 3, 0, 1, 0, "ViewSB" };
			desc.m_Slots[desc.m_SlotCount++] = { RHIBindingType::BindlessResourceTable,
				RHIShaderStage::Compute, 0, 0, 0, 0, "BindlessResources" };
			desc.m_Slots[desc.m_SlotCount++] = { RHIBindingType::BindlessSamplerTable,
				RHIShaderStage::Compute, 0, 0, 0, 0, "BindlessSamplers" };
			return desc;
		}
	}

	void RenderPassAerialPerspective::AddPass(RenderGraph& rg, const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		if (!context.IsRenderSceneReady() ||
			!context.GetDisplayViewRenderSettings().m_Lighting.m_EnableAerialPerspective ||
			!context.m_RenderScene.m_Atmosphere ||
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
		RHIPipelineHandle probePipeline{};
		if (context.GetDisplayViewRenderSettings().m_Lighting.m_EnableAerialProbe)
		{
			auto* rhiContext = services.m_Presentation->GetRHIContext();
			auto* device = services.m_Presentation->GetDevice();
			GGLAB_ASSERT_NOT_NULL(rhiContext);
			GGLAB_ASSERT_NOT_NULL(device);
			m_ProbeReadback.Initialize(*device, rhiContext->GetFrameSlotCount());
			m_ProbeReadback.ConsumeCompletedSlot(context.m_FrameSlotIndex);
			if (!m_ProbeRecipe.m_CSId.IsValid())
			{
				m_ProbeRecipe.m_CSId = services.m_ShaderPrograms->LoadProgram(
					shader_programs::AerialPerspectiveProbeCompute);
				m_ProbeRecipe.m_BindingLayout = rhiContext->GetPipelineSystem().CreateBindingLayout(
					BuildProbeBindingLayout());
			}
			probePipeline = services.m_PipelineResolver->Resolve(
				m_ProbeSlot, m_ProbeRecipe, GetInfo());
		}

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

		const RGTextureId surfaceColor = rg.GetBlackboard().Get<RGViewTargetsTable>(
			ViewTargetsTableName).GetViewTargets(displayViewId).m_SceneColor;
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
		if (!probePipeline.IsValid()) return;

		const uint32_t width = view.m_Width;
		const uint32_t height = view.m_Height;
		const uint64_t frameSerial = context.m_FrameSerial;
		const float worldScaleKm = atmosphereResources->m_Diagnostics.m_Parameters.m_World.m_W;
		rg.AddPass<ProbePassData>("Atmosphere.AerialProbe.Sample", RGPassEncoderType::Compute,
			[displayViewId, surfaceColor](RenderGraph::RGBuilder& builder, ProbePassData& data)
			{
				auto& blackboard = builder.GetBlackboard();
				auto& aerialResources = blackboard.Get<RGAerialPerspectiveResources>(
					AerialPerspectiveResourcesName);
				const auto& sceneDepth = blackboard.Get<RGSceneDepthResources>(
					SceneDepthResourcesName);
				const auto& targets = blackboard.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);
				data.m_Surface = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(surfaceColor, RGTextureAccess::Sample, RHIStage::ComputeShader));
				data.m_Composite = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(targets.m_SceneColor, RGTextureAccess::Sample, RHIStage::ComputeShader));
				data.m_Depth = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(sceneDepth.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader),
					sceneDepth.m_SrvDesc);
				data.m_Radiance = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(aerialResources.m_RadianceAtlas,
						RGTextureAccess::Sample, RHIStage::ComputeShader));
				data.m_Throughput = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(aerialResources.m_ThroughputAtlas,
						RGTextureAccess::Sample, RHIStage::ComputeShader));
				RHIBufferDesc outputDesc{};
				outputDesc.m_SizeInBytes = AerialPerspectiveProbeReadback::ReadbackSizeInBytes;
				outputDesc.m_StrideInBytes = sizeof(AerialPerspectiveProbeSample);
				aerialResources.m_ProbeBuffer = builder.CreateBuffer("Atmosphere.AerialProbeSamples", outputDesc);
				builder.WriteInPlace(aerialResources.m_ProbeBuffer,
					RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_Output = aerialResources.m_ProbeBuffer;
			},
			[services, probePipeline, samplerIndex, viewIndex, gridWidth, gridHeight,
				maxDistanceKm, worldScaleKm, width, height, frameSerial](RGExecuteContext& execute,
				ProbePassData& data)
			{
				ProbeParameters parameters{};
				parameters.m_SurfaceIndex = execute.GetViewDescriptor(data.m_Surface).m_Index;
				parameters.m_CompositeIndex = execute.GetViewDescriptor(data.m_Composite).m_Index;
				parameters.m_DepthIndex = execute.GetViewDescriptor(data.m_Depth).m_Index;
				parameters.m_RadianceIndex = execute.GetViewDescriptor(data.m_Radiance).m_Index;
				parameters.m_ThroughputIndex = execute.GetViewDescriptor(data.m_Throughput).m_Index;
				parameters.m_SamplerIndex = samplerIndex;
				parameters.m_ViewIndex = viewIndex;
				parameters.m_GridWidth = gridWidth;
				parameters.m_GridHeight = gridHeight;
				parameters.m_Width = width;
				parameters.m_Height = height;
				parameters.m_MaxDistanceKm = maxDistanceKm;
				parameters.m_WorldScaleKm = worldScaleKm;
				parameters.m_FrameSerialLow = static_cast<uint32_t>(frameSerial);
				parameters.m_FrameSerialHigh = static_cast<uint32_t>(frameSerial >> 32);
				auto* command = execute.GetDirectComputeCommandContext();
				command->SetPipeline(probePipeline);
				command->SetReadWriteBuffer(1, execute.GetBufferHandle(data.m_Output));
				command->SetReadOnlyBuffer(2,
					services.m_FrameBuffers->GetViewStructuredBuffer()->GetBufferHandle());
				command->SetPushConstants(0, parameters);
				command->Dispatch(1, 1, 1);
			});

		const uint32_t frameSlot = context.m_FrameSlotIndex;
		const uint64_t worldGeneration = services.m_Environment->GetBakingStatus().m_ActiveGeneration;
		const float fovRadians = view.m_FovRadians;
		const float manualEV100 = context.GetDisplayViewRenderSettings().m_Exposure.m_ManualEV100;
		auto* probeReadback = &m_ProbeReadback;
		rg.AddPass<ProbeReadbackPassData>("Atmosphere.AerialProbe.Readback", RGPassEncoderType::Copy,
			[probeReadback, frameSlot](RenderGraph::RGBuilder& builder, ProbeReadbackPassData& data)
			{
				builder.SideEffect();
				const auto& aerialResources = builder.GetBlackboard().Get<
					RGAerialPerspectiveResources>(AerialPerspectiveResourcesName);
				data.m_Source = builder.Read(aerialResources.m_ProbeBuffer,
					RGBufferAccess::CopySource, RHIStage::Copy);
				RHIBufferDesc readbackDesc{};
				readbackDesc.m_SizeInBytes = AerialPerspectiveProbeReadback::ReadbackSizeInBytes;
				readbackDesc.m_Usage = RHIBufferUsage::CopyDest;
				readbackDesc.m_MemoryUsage = RHIMemoryUsage::GpuToCpu;
				readbackDesc.m_DebugName = "Atmosphere.AerialProbeReadback";
				data.m_Destination = builder.ImportBuffer("Atmosphere.AerialProbeReadback",
					probeReadback->GetBuffer(frameSlot), readbackDesc,
					RGBufferAccess::CopyDest, RGContentValidity::Undefined);
				builder.WriteInPlace(data.m_Destination, RGBufferAccess::CopyDest, RHIStage::Copy);
			},
			[probeReadback, frameSlot, frameSerial, worldGeneration, fovRadians, manualEV100](
				RGExecuteContext& execute, ProbeReadbackPassData& data)
			{
				auto* command = execute.GetCopyCommandContext();
				command->CopyBuffer(execute.GetBufferHandle(data.m_Destination), 0,
					execute.GetBufferHandle(data.m_Source), 0,
					AerialPerspectiveProbeReadback::ReadbackSizeInBytes);
				probeReadback->MarkScheduled(frameSlot, frameSerial, worldGeneration,
					fovRadians, manualEV100);
			});
	}
}
