#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassGTAO.h"

#include "Graphics/Pipeline/GTAO.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHITextureValidation.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/Shader/ShaderManager.h"

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

		struct PassData
		{
			RGTextureViewId m_DepthSrv{};
			RGTextureViewId m_RawAOUav{};
			RGTextureViewId m_HalfDepthUav{};
			RGTextureViewId m_NormalUav{};
			RGTextureViewId m_SelectedOffsetUav{};
			GTAOEvaluatePassParameters m_Parameters{};
		};

		bool SupportsTypedUavStore(RHIDevice& device, RHIFormat format) noexcept
		{
			const RHITextureDesc textureDesc{
				.m_Format = format,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
				.m_Extent = { 1, 1, 1 },
			};
			auto viewDesc = MakeRHITexture2DViewDesc(format);
			viewDesc.m_Type = RHITextureViewType::UnorderedAccess;
			return device.QueryTextureViewSupport(textureDesc, viewDesc).IsSupported();
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
		m_IsAvailable = SupportsTypedUavStore(*device, RHIFormat::R16Float) &&
			SupportsTypedUavStore(*device, RHIFormat::R32Float) &&
			SupportsTypedUavStore(*device, RHIFormat::R16G16Float) &&
			SupportsTypedUavStore(*device, RHIFormat::R16G16B16A16Float);
		if (!m_IsAvailable)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"GTAO evaluate is unavailable because a required typed UAV store format is unsupported.");
			return;
		}

		ShaderDesc shaderDesc{};
		shaderDesc.m_SourcePath = L"Passes/PassGTAO.hlsl";
		shaderDesc.m_Stage = ShaderStage::Compute;
		shaderDesc.m_Entry = L"CSMain";
		m_PipelineRecipe.m_CSId = shaderManager->LoadShader(shaderDesc);
		m_PipelineRecipe.m_BindingLayout = renderer->GetCommonBindingLayout();
		if (!m_PipelineRecipe.m_CSId.IsValid() || !m_PipelineRecipe.m_BindingLayout.IsValid())
		{
			m_IsAvailable = false;
			GGLAB_LOG_GRAPHICS_ERROR("GTAO evaluate failed to prepare its compute pipeline recipe.");
		}
	}

	void RenderPassGTAO::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "GTAO evaluate must be prepared before graph construction.");
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
		const uint32_t viewIndex =
			static_cast<uint32_t>(utils::ToIndex(context.GetDisplayViewId()));

		rg.AddPass<PassData>(
			GetRenderGraphPassName(), RGPassEncoderType::Compute,
			[viewIndex, settings](RenderGraph::RGBuilder& builder, PassData& data)
			{
				auto& blackboard = builder.GetBlackboard();
				const auto& sceneDepth =
					blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				GGLAB_ASSERT_MSG(sceneDepth.m_Convention == DepthConvention::Reversed,
					"GTAO evaluate currently requires Reversed-Z display depth.");

				const RHITextureDesc& depthDesc = builder.GetTextureDesc(sceneDepth.m_Texture);
				const GTAOExtent halfExtent = MakeGTAOHalfResolutionExtent(
					depthDesc.m_Extent.m_Width, depthDesc.m_Extent.m_Height);
				GGLAB_ASSERT_MSG(halfExtent.IsValid(), "GTAO requires a non-empty display extent.");

				RHITextureDesc outputDesc{};
				outputDesc.m_Extent = { halfExtent.m_Width, halfExtent.m_Height, 1 };
				auto& resources = blackboard.Get<RGGTAOResources>(GTAOResourcesName);
				outputDesc.m_Format = RHIFormat::R16Float;
				resources.m_RawAO = builder.CreateTexture("GTAO.RawAO", outputDesc);
				outputDesc.m_Format = RHIFormat::R32Float;
				resources.m_HalfDepthViewZ =
					builder.CreateTexture("GTAO.HalfDepthViewZ", outputDesc);
				outputDesc.m_Format = RHIFormat::R16G16B16A16Float;
				resources.m_ReconstructedNormal =
					builder.CreateTexture("GTAO.ReconstructedNormal", outputDesc);
				outputDesc.m_Format = RHIFormat::R16G16Float;
				resources.m_SelectedSurfaceOffset =
					builder.CreateTexture("GTAO.SelectedSurfaceOffset", outputDesc);
				resources.m_FullWidth = depthDesc.m_Extent.m_Width;
				resources.m_FullHeight = depthDesc.m_Extent.m_Height;
				resources.m_HalfWidth = halfExtent.m_Width;
				resources.m_HalfHeight = halfExtent.m_Height;

				const RGTextureId depth = builder.Read(
					sceneDepth.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_DepthSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					depth, sceneDepth.m_SrvDesc);
				builder.WriteInPlace(resources.m_RawAO, RGTextureAccess::StorageWrite,
					RHIStage::ComputeShader);
				builder.WriteInPlace(resources.m_HalfDepthViewZ, RGTextureAccess::StorageWrite,
					RHIStage::ComputeShader);
				builder.WriteInPlace(resources.m_ReconstructedNormal, RGTextureAccess::StorageWrite,
					RHIStage::ComputeShader);
				builder.WriteInPlace(resources.m_SelectedSurfaceOffset, RGTextureAccess::StorageWrite,
					RHIStage::ComputeShader);
				data.m_RawAOUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					resources.m_RawAO);
				data.m_HalfDepthUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					resources.m_HalfDepthViewZ);
				data.m_NormalUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					resources.m_ReconstructedNormal);
				data.m_SelectedOffsetUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
					resources.m_SelectedSurfaceOffset);

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
			[this, renderer, &context](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetDirectComputeCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				const auto depthSrv = executeContext.GetViewDescriptor(data.m_DepthSrv);
				const auto rawAOUav = executeContext.GetViewDescriptor(data.m_RawAOUav);
				const auto halfDepthUav = executeContext.GetViewDescriptor(data.m_HalfDepthUav);
				const auto normalUav = executeContext.GetViewDescriptor(data.m_NormalUav);
				const auto selectedOffsetUav =
					executeContext.GetViewDescriptor(data.m_SelectedOffsetUav);
				GGLAB_ASSERT_MSG(depthSrv.IsValid() && rawAOUav.IsValid() && halfDepthUav.IsValid() &&
					normalUav.IsValid() && selectedOffsetUav.IsValid(),
					"GTAO graph views must be shader visible before dispatch.");

				auto parameters = data.m_Parameters;
				parameters.m_DepthTextureIndex = depthSrv.m_Index;
				parameters.m_RawAOUavIndex = rawAOUav.m_Index;
				parameters.m_HalfDepthUavIndex = halfDepthUav.m_Index;
				parameters.m_NormalUavIndex = normalUav.m_Index;
				parameters.m_SelectedOffsetUavIndex = selectedOffsetUav.m_Index;
				commandContext->SetPipeline(GetOrCreatePipeline(*renderer));
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
	}

	RHIPipelineHandle RenderPassGTAO::GetOrCreatePipeline(const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_PipelineRecipe, GetInfo());
	}
}
