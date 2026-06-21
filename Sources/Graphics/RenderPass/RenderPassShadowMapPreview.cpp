#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassShadowMapPreview.h"
#include "Graphics/Renderer.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGShadowResources.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorManager.h"

namespace gglab
{
	namespace
	{
		struct ShadowMapPreviewPassParameters
		{
			uint32_t ShadowMapTextureIndex = 0;
			uint32_t ShadowMapSamplerIndex = 0;
			float PreviewMinDepth = 0.0f;
			float PreviewMaxDepth = 1.0f;
			uint32_t PreviewInvert = 0;
			uint32_t Padding[3]{};
		};
		static_assert(IsPassRootConstantStruct<ShadowMapPreviewPassParameters>);
		static_assert(sizeof(ShadowMapPreviewPassParameters) == 32);

		struct PassData
		{
			RGTextureId m_ShadowMap{};
			RGTextureId m_ShadowMapPreview{};
			RGTextureViewId m_ShadowMapSrv{};
			RGTextureViewId m_PreviewRtv{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_SamplerIndex = 0;
			float m_MinDepth = 0.0f;
			float m_MaxDepth = 1.0f;
			uint32_t m_Invert = 0;
		};
	}

	void RenderPassShadowMapPreview::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		auto* contextPtr = &context;
		GGLAB_ASSERT_NOT_NULL(contextPtr);

		auto* servicesPtr = &services;
		GGLAB_ASSERT_NOT_NULL(servicesPtr);

		EnsureInitialized(services);

		rg.AddPass<PassData>("RenderPassShadowMapPreview",
			[contextPtr, servicesPtr](RenderGraph::RGBuilder& builder, PassData& data)
			{
				auto& shadowRes = builder.GetBlackboard().Get<RGShadowResources>(ShadowResourcesName);

				data.m_ShadowMap = builder.Read(shadowRes.m_DirectionalShadowMap, RGTextureUsage::Sample);
				data.m_ShadowMapPreview = builder.Write(
					shadowRes.m_DirectionalShadowMapPreview,
					RGTextureUsage::RenderTarget);

				D3D12_SHADER_RESOURCE_VIEW_DESC shadowMapSrvDesc{};
				shadowMapSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
				shadowMapSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				shadowMapSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				shadowMapSrvDesc.Texture2D.MostDetailedMip = 0;
				shadowMapSrvDesc.Texture2D.MipLevels = 1;
				shadowMapSrvDesc.Texture2D.PlaneSlice = 0;
				shadowMapSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
				data.m_ShadowMapSrv =
					builder.CreateView<ViewType::SRV>(data.m_ShadowMap, shadowMapSrvDesc);
				data.m_PreviewRtv =
					builder.CreateView<ViewType::RTV>(data.m_ShadowMapPreview);

				data.m_Width = shadowRes.m_ShadowMapPreviewSize;
				data.m_Height = shadowRes.m_ShadowMapPreviewSize;

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);
				data.m_SamplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(SamplerPreset::PointClamp);

				const auto& settings = contextPtr->GetShadowVisualizationSettings();
				data.m_MinDepth = std::clamp(settings.m_PreviewMinDepth, 0.0f, 1.0f);
				data.m_MaxDepth = std::clamp(settings.m_PreviewMaxDepth, 0.0f, 1.0f);
				data.m_Invert = settings.m_PreviewInvert ? 1u : 0u;
			},
			[this, &rg, contextPtr, servicesPtr](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandList = executeContext.m_GraphicsCommandList;
				GGLAB_ASSERT_NOT_NULL(commandList);

				auto* previewTexture = rg.GetTexture(data.m_ShadowMapPreview);
				GGLAB_ASSERT_NOT_NULL(previewTexture);

				const auto shadowMapSrv = executeContext.GetView(data.m_ShadowMapSrv);
				GGLAB_ASSERT_MSG(shadowMapSrv.m_Index != std::numeric_limits<uint32_t>::max(),
					"Shadow map preview SRV must expose a descriptor heap index.");

				const auto previewRtv = executeContext.GetView(data.m_PreviewRtv);
				commandList->ClearRenderTarget(previewRtv, *previewTexture);

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);

				auto* descriptorManager = renderer->GetDescriptorManager();
				GGLAB_ASSERT_NOT_NULL(descriptorManager);

				const DX12DescriptorHeap* descriptorHeaps[] = {
					descriptorManager->GetHeap(DX12DescriptorManager::HeapType::CbvSrvUav),
					descriptorManager->GetHeap(DX12DescriptorManager::HeapType::Sampler)
				};
				commandList->SetDescriptorHeaps(descriptorHeaps);
				commandList->SetGraphicsRootSignature(*renderer->GetCommonRootSignature());
				commandList->SetPipelineState(*GetOrCreatePSO(*renderer));
				commandList->SetRenderTarget(previewRtv);
				commandList->SetViewport(0, 0, data.m_Width, data.m_Height);
				commandList->SetScissorRect(0, 0, data.m_Width, data.m_Height);
				commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				commandList->SetGraphicsConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					renderer->GetSceneConstantBuffer()->GetGPUVirtualAddress(contextPtr->m_BackBufferIndex));

				const ShadowMapPreviewPassParameters passParameters{
					.ShadowMapTextureIndex = shadowMapSrv.m_Index,
					.ShadowMapSamplerIndex = data.m_SamplerIndex,
					.PreviewMinDepth = data.m_MinDepth,
					.PreviewMaxDepth = data.m_MaxDepth,
					.PreviewInvert = data.m_Invert,
				};
				commandList->SetGraphicsRoot32BitConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					passParameters);

				commandList->DrawInstanced(3);
			});
	}

	void RenderPassShadowMapPreview::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassShadowMapPreview.hlsl";
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
			m_BaseRecipe.m_Formats.m_RtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
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

	DX12PipelineState* RenderPassShadowMapPreview::GetOrCreatePSO(const Renderer& renderer) noexcept
	{
		auto* passRegistry = renderer.GetRenderPassRecipeRegistry();
		GGLAB_ASSERT_NOT_NULL(passRegistry);

		auto* psoCache = renderer.GetPSOCache();
		GGLAB_ASSERT_NOT_NULL(psoCache);

		const auto& cached =
			passRegistry->GetOrCreateGraphics("RenderPassShadowMapPreview", m_BaseRecipe);

		return psoCache->GetOrCreate(cached.m_Key, cached.m_Desc);
	}
}
