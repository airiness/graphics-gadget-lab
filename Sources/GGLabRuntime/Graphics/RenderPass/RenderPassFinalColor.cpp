#include "Graphics/RenderPass/RenderPassFinalColor.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Renderer.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/PostProcess/PostProcessGraphResources.h"
#include "Graphics/RenderGraph/RenderGraph.h"

#include <cstdint>
#include <span>

namespace gglab
{
	namespace
	{
		struct FinalColorPassParameters
		{
			uint32_t SceneColorTextureIndex = 0;
			uint32_t SceneColorSamplerIndex = 0;
			uint32_t BloomTextureIndex = 0;
			uint32_t BloomSamplerIndex = 0;
			uint32_t ViewIndex = 0;
			uint32_t BloomEnabled = 0;
			float BloomIntensity = 0.0f;
			float ScenePreExposure = 1.0f;
		};
		static_assert(IsPassRootConstantStruct<FinalColorPassParameters>);
		static_assert(sizeof(FinalColorPassParameters) == 32);

		struct PassData
		{
			RGTextureId m_SceneColor{};
			RGTextureId m_Bloom{};
			RGTextureId m_Output{};
			RGTextureViewId m_SceneColorSrv{};
			RGTextureViewId m_BloomSrv{};
			RGTextureViewId m_OutputRtv{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_SamplerIndex = 0;
			bool m_BloomEnabled = false;
			float m_BloomIntensity = 0.0f;
			float m_ScenePreExposure = 1.0f;
		};
	}

	void RenderPassFinalColor::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		auto* contextPtr = &context;
		GGLAB_ASSERT_NOT_NULL(contextPtr);

		auto* servicesPtr = &services;
		GGLAB_ASSERT_NOT_NULL(servicesPtr);

		EnsureInitialized(services);
		const RenderViewID displayViewId = context.GetDisplayViewId();

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[contextPtr, servicesPtr, displayViewId](
				RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& postProcess =
					builder.GetBlackboard().Get<RGPostProcessResources>(PostProcessResourcesName);
				GGLAB_ASSERT_MSG(postProcess.m_Inputs.m_SceneColor.m_State ==
					PostProcessColorState::SceneLinearRec709,
					"FinalColor requires scene-linear Rec.709 input.");
				GGLAB_ASSERT_MSG(postProcess.m_Inputs.m_SceneColor.m_PreExposure > 0.0f,
					"FinalColor requires a positive scene pre-exposure.");
				GGLAB_ASSERT_MSG(
					postProcess.m_Output.m_Transform.m_Mode == OutputColorMode::SdrSRGB,
					"FinalColor currently supports only SDR sRGB output.");

				const auto& viewSettings = contextPtr->GetViewRenderSettings(displayViewId);
				GGLAB_ASSERT_MSG(viewSettings.m_PostProcess.m_ToneMapping.m_Operator ==
					ToneMappingOperator::AcesFitted,
					"FinalColor currently supports only ACES fitted tone mapping.");

				data.m_SceneColor = builder.Read(
					postProcess.m_Inputs.m_SceneColor.m_Texture, RGTextureAccess::Sample);
				builder.WriteInPlace(postProcess.m_Output.m_Texture, RGTextureAccess::RenderTarget);
				data.m_Output = postProcess.m_Output.m_Texture;
				data.m_SceneColorSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(data.m_SceneColor);
				data.m_BloomEnabled = postProcess.m_Bloom.m_Result.m_Texture.IsValid();
				if (data.m_BloomEnabled)
				{
					GGLAB_ASSERT_MSG(postProcess.m_Bloom.m_Result.m_State ==
						PostProcessColorState::SceneLinearRec709,
						"FinalColor requires a scene-linear Rec.709 bloom contribution.");
					GGLAB_ASSERT_MSG(postProcess.m_Bloom.m_Result.m_PreExposure ==
						postProcess.m_Inputs.m_SceneColor.m_PreExposure,
						"FinalColor requires scene color and bloom to use the same pre-exposure.");
					data.m_Bloom = builder.Read(
						postProcess.m_Bloom.m_Result.m_Texture, RGTextureAccess::Sample);
					data.m_BloomSrv =
						builder.CreateView<RHITextureViewType::ShaderResource>(data.m_Bloom);
				}
				else
				{
					data.m_Bloom = data.m_SceneColor;
					data.m_BloomSrv = data.m_SceneColorSrv;
				}
				data.m_OutputRtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_Output);

				const RenderView& displayView = contextPtr->GetDisplayRenderView();
				data.m_Width = displayView.m_Width;
				data.m_Height = displayView.m_Height;

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);
				data.m_SamplerIndex =
					renderer->GetSamplerRegistry()->GetSamplerIndex(SamplerPreset::LinearClamp);
				data.m_BloomIntensity = viewSettings.m_PostProcess.m_Bloom.m_Intensity;
				data.m_ScenePreExposure = postProcess.m_Inputs.m_SceneColor.m_PreExposure;
			},
			[this, contextPtr, servicesPtr, displayViewId](
				RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto sceneColorSrv = executeContext.GetViewDescriptor(data.m_SceneColorSrv);
				const auto bloomSrv = executeContext.GetViewDescriptor(data.m_BloomSrv);
				GGLAB_ASSERT_MSG(sceneColorSrv.IsValid(),
					"FinalColor scene color SRV must expose a descriptor heap index.");
				GGLAB_ASSERT_MSG(bloomSrv.IsValid(),
					"FinalColor bloom SRV must expose a descriptor heap index.");

				const auto outputRtv = executeContext.GetViewHandle(data.m_OutputRtv);

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);

				commandContext->SetPipeline(GetOrCreatePSO(*renderer));
				const RHIRenderingAttachment colorAttachment{
					.m_View = outputRtv,
					.m_LoadOp = RHIContentLoadOp::DontCare,
				};
				commandContext->BeginRendering({ .m_ColorAttachments =
					std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width),
					static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width),
					static_cast<int32_t>(data.m_Height) });

				const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					contextPtr->m_RenderScene.m_SceneConstantBufferOffset);

				const auto& viewSB = renderer->GetViewStructuredBuffer();
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					viewSB->GetBufferHandle());

				const FinalColorPassParameters passParameters{
					.SceneColorTextureIndex = sceneColorSrv.m_Index,
					.SceneColorSamplerIndex = data.m_SamplerIndex,
					.BloomTextureIndex = bloomSrv.m_Index,
					.BloomSamplerIndex = data.m_SamplerIndex,
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(displayViewId)),
					.BloomEnabled = data.m_BloomEnabled ? 1u : 0u,
					.BloomIntensity = data.m_BloomIntensity,
					.ScenePreExposure = data.m_ScenePreExposure,
				};
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), passParameters);

				commandContext->DrawFullscreenTriangle();
			});
	}

	void RenderPassFinalColor::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Passes/PassFinalColor.hlsl";
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
			m_BaseRecipe.m_Formats.m_RenderTargetFormats[0] = renderer->GetSwapChain()->GetFormat();
			m_BaseRecipe.m_Formats.m_RenderTargetCount = 1;
			m_BaseRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::Unknown;
			m_BaseRecipe.m_Formats.m_SampleCount = 1;
			m_BaseRecipe.m_Formats.m_SampleQuality = 0;

			m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
			m_BaseRecipe.m_BlendPreset = BlendPreset::Default;
			m_BaseRecipe.m_DepthPreset = DepthPreset::DepthDisabled;

			m_IsInitialized = true;
		}
	}

	RHIPipelineHandle RenderPassFinalColor::GetOrCreatePSO(const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_BaseRecipe, GetInfo());
	}
}
