#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassSkybox.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/IBLGraphResources.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/Shader/ShaderManager.h"

namespace gglab
{
	namespace
	{
		struct SkyboxPassParameters
		{
			uint32_t ViewIndex = 0;
			uint32_t Padding[3]{};
		};
		static_assert(IsPassRootConstantStruct<SkyboxPassParameters>);
		static_assert(sizeof(SkyboxPassParameters) == 16);

		struct PassData
		{
			RGTextureId m_EnvironmentCubemap{};
			RGTextureId m_SceneColor{};
			RGTextureViewId m_Rtv{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};
	}

	void RenderPassSkybox::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		if (!context.IsRenderSceneReady())
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		const auto* environmentSystem = renderer->GetEnvironmentLightingSystem();
		if (!environmentSystem || !environmentSystem->GetSettings().m_EnableSkybox)
		{
			return;
		}

		EnsureInitialized(services);
		const RenderViewID displayViewId = context.GetDisplayViewId();
		const auto* contextPtr = &context;

		rg.AddPass<PassData>(GetRenderGraphPassName(),
			[displayViewId](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& iblResources = blackboard.Get<RGIBLResources>(IBLResourcesName);
				auto& targets = blackboard
					.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);

				data.m_EnvironmentCubemap = builder.Read(
					iblResources.m_EnvironmentCubemap,
					RGTextureAccess::Sample);
				builder.WriteInPlace(targets.m_SceneColor, RGTextureAccess::RenderTarget);
				data.m_SceneColor = targets.m_SceneColor;
				data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_SceneColor);
				data.m_Width = targets.m_Width;
				data.m_Height = targets.m_Height;
			},
			[this, renderer, contextPtr, displayViewId](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);

				commandContext->SetPipeline(GetOrCreatePSO(*renderer));
				commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&rtv, 1));
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width), static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width), static_cast<int32_t>(data.m_Height) });
				commandContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

				const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					contextPtr->m_RenderScene.m_SceneConstantBufferOffset);

				const auto* viewBuffer = renderer->GetViewStructuredBuffer();
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					viewBuffer->GetBufferHandle());

				const SkyboxPassParameters passParameters{
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(displayViewId)),
				};
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					passParameters);
				commandContext->Draw(3);
			});
	}

	void RenderPassSkybox::EnsureInitialized(const RenderServices& services) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(renderer);
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		ShaderDesc shaderDesc{};
		shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassSkybox.hlsl";
		shaderDesc.m_Stage = ShaderStage::Vertex;
		shaderDesc.m_Entry = L"VSMain";
		const auto vsId = shaderManager->LoadShader(shaderDesc);

		shaderDesc.m_Stage = ShaderStage::Pixel;
		shaderDesc.m_Entry = L"PSMain";
		const auto psId = shaderManager->LoadShader(shaderDesc);

		m_BaseRecipe.m_BindingLayout = renderer->GetCommonBindingLayout();
		m_BaseRecipe.m_InputLayoutId = InputLayoutID::None;
		m_BaseRecipe.m_VSId = vsId;
		m_BaseRecipe.m_PSId = psId;
		m_BaseRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		m_BaseRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		m_BaseRecipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R16G16B16A16Float;
		m_BaseRecipe.m_Formats.m_RenderTargetCount = 1;
		m_BaseRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::Unknown;
		m_BaseRecipe.m_Formats.m_SampleCount = 1;
		m_BaseRecipe.m_Formats.m_SampleQuality = 0;
		m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
		m_BaseRecipe.m_BlendPreset = BlendPreset::Default;
		m_BaseRecipe.m_DepthPreset = DepthPreset::DepthDisabled;
		m_IsInitialized = true;
	}

	RHIPipelineHandle RenderPassSkybox::GetOrCreatePSO(const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_BaseRecipe, GetInfo());
	}
}
