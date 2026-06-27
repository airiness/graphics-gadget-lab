#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassTonemap.h"
#include "Graphics/Renderer.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGFrameTargets.h"
#include "Graphics/RHI/DX12/DX12SwapChain.h"

namespace gglab
{
	namespace
	{
		struct TonemapPassParameters
		{
			uint32_t SceneColorTextureIndex = 0;
			uint32_t SceneColorSamplerIndex = 0;
			uint32_t ViewIndex = 0;
			uint32_t Padding = 0;
		};
		static_assert(IsPassRootConstantStruct<TonemapPassParameters>);
		static_assert(sizeof(TonemapPassParameters) == 16);

		struct PassData
		{
			RGTextureId m_SceneColor{};
			RGTextureId m_BackBuffer{};
			RGTextureViewId m_SceneColorSrv{};
			RGTextureViewId m_BackBufferRtv{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_SamplerIndex = 0;
		};
	}

	void RenderPassTonemap::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		auto* contextPtr = &context;
		GGLAB_ASSERT_NOT_NULL(contextPtr);

		auto* servicesPtr = &services;
		GGLAB_ASSERT_NOT_NULL(servicesPtr);

		EnsureInitialized(services);

		rg.AddPass<PassData>("RenderPassTonemap",
			[servicesPtr](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& targetsTable = builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName);
				auto& mainTargets = targetsTable.GetViewTargets(RenderViewID::Main);

				data.m_SceneColor = builder.Read(mainTargets.m_SceneColor, RGTextureAccess::Sample);
				data.m_BackBuffer = builder.Write(mainTargets.m_BackBuffer, RGTextureAccess::RenderTarget);
				data.m_SceneColorSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(data.m_SceneColor);
				data.m_BackBufferRtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_BackBuffer);
				data.m_Width = mainTargets.m_Width;
				data.m_Height = mainTargets.m_Height;

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);
				data.m_SamplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(SamplerPreset::LinearClamp);
			},
			[this, contextPtr, servicesPtr](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto sceneColorSrv = executeContext.GetViewDescriptor(data.m_SceneColorSrv);
				GGLAB_ASSERT_MSG(sceneColorSrv.IsValid(),
					"Tonemap scene color SRV must expose a descriptor heap index.");

				const auto backBufferRtv = executeContext.GetViewHandle(data.m_BackBufferRtv);

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);

				commandContext->SetPipeline(GetOrCreatePSO(*renderer));
				commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&backBufferRtv, 1));
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width), static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width), static_cast<int32_t>(data.m_Height) });
				commandContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

				const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					contextPtr->m_RenderScene.m_SceneConstantBufferOffset);

				const auto& viewSB = renderer->GetViewStructuredBuffer();
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					viewSB->GetBufferHandle());

				const TonemapPassParameters passParameters{
					.SceneColorTextureIndex = sceneColorSrv.m_Index,
					.SceneColorSamplerIndex = data.m_SamplerIndex,
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(RenderViewID::Main)),
				};
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					passParameters);

				commandContext->Draw(3);
			});
	}

	void RenderPassTonemap::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassTonemap.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			const auto vsId = shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			const auto psId = shaderManager->LoadShader(shaderDesc);

			m_BaseRecipe.m_RootSignatureId = renderer->GetCommonRootSignatureId();
			m_BaseRecipe.m_InputLayoutId = InputLayoutID::None;
			m_BaseRecipe.m_VSId = vsId;
			m_BaseRecipe.m_PSId = psId;

			m_BaseRecipe.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			m_BaseRecipe.m_Formats.m_RtvFormats.RTFormats[0] = renderer->GetSwapChain()->GetFormat();
			m_BaseRecipe.m_Formats.m_RtvFormats.NumRenderTargets = 1;
			m_BaseRecipe.m_Formats.m_DsvFormat = DXGI_FORMAT_UNKNOWN;
			m_BaseRecipe.m_Formats.m_SampleCount = 1;
			m_BaseRecipe.m_Formats.m_SampleQuality = 0;

			m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
			m_BaseRecipe.m_BlendPreset = BlendPreset::Default;
			m_BaseRecipe.m_DepthPreset = DepthPreset::DepthDisabled;

			m_IsInitialized = true;
		}

	}

	RHIPipelineHandle RenderPassTonemap::GetOrCreatePSO(const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_BaseRecipe);
	}
}
