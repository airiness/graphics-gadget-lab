#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBLDebugPreview.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGIBLResources.h"
#include "Graphics/RenderGraph/RGIBLDebugPreviewResources.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/DX12/Cache/InputLayoutLibrary.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/SamplerRegistry.h"

namespace gglab
{
	void RenderPassIBLDebugPreview::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* renderResRegistry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);
		renderResRegistry->EnsureIblResources();

		EnsureInitialized(services);

		struct PreviewData
		{
			RGTextureId m_EnvironmentCubemap{};
			RGTextureId m_EnvironmentCubemapPreview{};
			ViewKey m_RtvKey{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_DisplayLayout = 0;
			uint32_t m_EnvironmentTextureIndex = 0;
			uint32_t m_EnvironmentSamplerIndex = 0;
		};

		auto* contextPtr = &context;
		rg.AddPass<PreviewData>("RenderPassIBLDebugPreview.EnvironmentCubemap",
			[renderer, renderResRegistry](RenderGraph::RGBuilder& builder, PreviewData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);
				auto& debugPreviewRes = blackboard.GetOrCreate<RGIBLDebugPreviewResources>(IBLDebugPreviewResourcesName);

				data.m_EnvironmentCubemap = builder.Read(iblRes.m_EnvironmentCubemap, RGTextureUsage::Sample);

				auto* previewTexture = renderResRegistry->GetTexture(
					RenderResourceRegistry::TextureIndex::DebugPreview_IBL_EnvironmentCubemap);
				GGLAB_ASSERT_NOT_NULL(previewTexture);

				const auto previewDesc = ToRGTextureDesc(*previewTexture,
					RGTextureUsage::RenderTarget | RGTextureUsage::Sample);

				debugPreviewRes.m_EnvironmentCubemapPreview = builder.ImportTexture(
					"DebugPreview.IBL.EnvironmentCubemap",
					previewTexture,
					previewDesc,
					D3D12_RESOURCE_STATE_COMMON);

				data.m_EnvironmentCubemapPreview = builder.Write(
					debugPreviewRes.m_EnvironmentCubemapPreview,
					RGTextureUsage::RenderTarget);

				const auto externalIndex = renderResRegistry->GetExternalIndex(
					RenderResourceRegistry::TextureIndex::DebugPreview_IBL_EnvironmentCubemap);
				GGLAB_ASSERT_MSG(ExternalResourceIndex::IsExternal(externalIndex),
					"IBL Environment cubemap debug preview must be imported as an external RenderGraph resource.");

				data.m_RtvKey = DX12ViewCache::BuildKey<ViewType::RTV>(externalIndex, previewTexture);
				data.m_Width = previewDesc.m_Width;
				data.m_Height = previewDesc.m_Height;
				data.m_DisplayLayout = static_cast<uint32_t>(renderResRegistry->GetIBLDebugPreviewLayout());
				data.m_EnvironmentTextureIndex = renderResRegistry->GetBindlessSrvIndex(
					RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
				data.m_EnvironmentSamplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(
					SamplerPreset::LinearClamp);
			},
			[this, &rg, renderer, contextPtr](RGExecuteContext& executeContext, PreviewData& data)
			{
				auto* commandList = executeContext.m_GraphicsCommandList;
				GGLAB_ASSERT_NOT_NULL(commandList);

				auto* environmentTexture = rg.GetTexture(data.m_EnvironmentCubemap);
				GGLAB_ASSERT_NOT_NULL(environmentTexture);

				auto* previewTexture = rg.GetTexture(data.m_EnvironmentCubemapPreview);
				GGLAB_ASSERT_NOT_NULL(previewTexture);

				auto* viewCache = rg.GetViewCache();
				GGLAB_ASSERT_NOT_NULL(viewCache);

				{
					CD3DX12_TEXTURE_BARRIER environmentToShaderResource(
						D3D12_BARRIER_SYNC_ALL,
						D3D12_BARRIER_SYNC_PIXEL_SHADING,
						D3D12_BARRIER_ACCESS_COMMON,
						D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
						D3D12_BARRIER_LAYOUT_COMMON,
						D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
						environmentTexture->Get(),
						CD3DX12_BARRIER_SUBRESOURCE_RANGE(0, 1, 0, CubemapFaceCount));

					CD3DX12_TEXTURE_BARRIER previewToRenderTarget(
						D3D12_BARRIER_SYNC_ALL,
						D3D12_BARRIER_SYNC_RENDER_TARGET,
						D3D12_BARRIER_ACCESS_COMMON,
						D3D12_BARRIER_ACCESS_RENDER_TARGET,
						D3D12_BARRIER_LAYOUT_COMMON,
						D3D12_BARRIER_LAYOUT_RENDER_TARGET,
						previewTexture->Get(),
						CD3DX12_BARRIER_SUBRESOURCE_RANGE(0));

					commandList->AddTextureBarrier(environmentToShaderResource);
					commandList->AddTextureBarrier(previewToRenderTarget);
					commandList->FlushBarriers();
				}

				const auto rtv = viewCache->GetOrCreate(data.m_RtvKey, previewTexture);
				commandList->ClearRenderTarget(rtv, *previewTexture);

				auto* pso = GetOrCreateEnvironmentCubemapPreviewPSO(*renderer);
				GGLAB_ASSERT_NOT_NULL(pso);

				auto* rootSignature = renderer->GetCommonRootSignature();
				GGLAB_ASSERT_NOT_NULL(rootSignature);

				auto* descriptorManager = renderer->GetDescriptorManager();
				GGLAB_ASSERT_NOT_NULL(descriptorManager);

				const DX12DescriptorHeap* descriptorHeaps[] = {
					descriptorManager->GetHeap(DX12DescriptorManager::HeapType::CbvSrvUav),
					descriptorManager->GetHeap(DX12DescriptorManager::HeapType::Sampler)
				};
				commandList->SetDescriptorHeaps(descriptorHeaps);
				commandList->SetGraphicsRootSignature(*rootSignature);
				commandList->SetPipelineState(*pso);
				commandList->SetRenderTarget(rtv);
				commandList->SetViewport(0, 0, data.m_Width, data.m_Height);
				commandList->SetScissorRect(0, 0, data.m_Width, data.m_Height);
				commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				commandList->SetGraphicsConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					renderer->GetSceneConstantBuffer()->GetGPUVirtualAddress(contextPtr->m_BackBufferIndex));

				const uint32_t localConstants[] = {
					data.m_DisplayLayout,
					data.m_EnvironmentTextureIndex,
					data.m_EnvironmentSamplerIndex,
				};
				commandList->SetGraphicsRoot32BitConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::LocalConstants),
					localConstants);

				commandList->DrawInstanced(3);

				{
					CD3DX12_TEXTURE_BARRIER previewToCommon(
						D3D12_BARRIER_SYNC_RENDER_TARGET,
						D3D12_BARRIER_SYNC_ALL,
						D3D12_BARRIER_ACCESS_RENDER_TARGET,
						D3D12_BARRIER_ACCESS_COMMON,
						D3D12_BARRIER_LAYOUT_RENDER_TARGET,
						D3D12_BARRIER_LAYOUT_COMMON,
						previewTexture->Get(),
						CD3DX12_BARRIER_SUBRESOURCE_RANGE(0));

					CD3DX12_TEXTURE_BARRIER environmentToCommon(
						D3D12_BARRIER_SYNC_PIXEL_SHADING,
						D3D12_BARRIER_SYNC_ALL,
						D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
						D3D12_BARRIER_ACCESS_COMMON,
						D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
						D3D12_BARRIER_LAYOUT_COMMON,
						environmentTexture->Get(),
						CD3DX12_BARRIER_SUBRESOURCE_RANGE(0, 1, 0, CubemapFaceCount));

					commandList->AddTextureBarrier(previewToCommon);
					commandList->AddTextureBarrier(environmentToCommon);
					commandList->FlushBarriers();
				}
			});
	}

	void RenderPassIBLDebugPreview::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassIBLEnvironmentDebugPreview.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			const auto vsId = shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			const auto psId = shaderManager->LoadShader(shaderDesc);

			m_EnvironmentCubemapPreviewInputs.m_RootSignatureId = renderer->GetCommonRootSignatureId();
			m_EnvironmentCubemapPreviewInputs.m_InputLayoutId = InputLayoutID::None;
			m_EnvironmentCubemapPreviewInputs.m_VSId = vsId;
			m_EnvironmentCubemapPreviewInputs.m_PSId = psId;

			m_EnvironmentCubemapPreviewInputs.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			m_EnvironmentCubemapPreviewInputs.m_Formats.m_RtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			m_EnvironmentCubemapPreviewInputs.m_Formats.m_RtvFormats.NumRenderTargets = 1;
			m_EnvironmentCubemapPreviewInputs.m_Formats.m_DsvFormat = DXGI_FORMAT_UNKNOWN;
			m_EnvironmentCubemapPreviewInputs.m_Formats.m_SampleCount = 1;
			m_EnvironmentCubemapPreviewInputs.m_Formats.m_SampleQuality = 0;

			m_EnvironmentCubemapPreviewInputs.m_RasterizerPreset = RasterizerPreset::Default;
			m_EnvironmentCubemapPreviewInputs.m_BlendPreset = BlendPreset::Default;
			m_EnvironmentCubemapPreviewInputs.m_DepthPreset = DepthPreset::DepthDisabled;

			m_IsInitialized = true;
		}

		m_EnvironmentCubemapPreviewInputs.m_VSGen =
			shaderManager->GetGeneration(m_EnvironmentCubemapPreviewInputs.m_VSId);
		m_EnvironmentCubemapPreviewInputs.m_PSGen =
			shaderManager->GetGeneration(m_EnvironmentCubemapPreviewInputs.m_PSId);
	}

	DX12PipelineState* RenderPassIBLDebugPreview::GetOrCreateEnvironmentCubemapPreviewPSO(
		const Renderer& renderer) noexcept
	{
		auto* passRegistry = renderer.GetRenderPassRecipeRegistry();
		GGLAB_ASSERT_NOT_NULL(passRegistry);

		auto* psoCache = renderer.GetPSOCache();
		GGLAB_ASSERT_NOT_NULL(psoCache);

		GraphicsKeyInputs inputs = m_EnvironmentCubemapPreviewInputs;
		const auto& cached = passRegistry->GetOrCreateGraphics("RenderPassIBLDebugPreview.EnvironmentCubemap",
			inputs,
			[&renderer](GraphicsPipelineDesc& outDesc, const GraphicsKeyInputs& input, ShaderManager* shaderManager)
			{
				outDesc.m_RootSignatureId = input.m_RootSignatureId;
				outDesc.m_RootSignature = renderer.GetRootSignatureCache()->GetDX12RootSignature(input.m_RootSignatureId)->Get();
				outDesc.m_InputLayoutId = input.m_InputLayoutId;
				outDesc.m_InputLayoutDesc = InputLayoutLibrary::Get(input.m_InputLayoutId);
				outDesc.m_VertexShader = shaderManager->GetBytecode(input.m_VSId);
				outDesc.m_PixelShader = shaderManager->GetBytecode(input.m_PSId);
				outDesc.m_Topology = input.m_Topology;
				outDesc.m_SampleMask = input.m_SampleMask;
				outDesc.m_Formats = input.m_Formats;
				outDesc.m_RasterizerDesc = ApplyRasterizerPreset(input.m_RasterizerPreset);
				outDesc.m_BlendDesc = ApplyBlendPreset(input.m_BlendPreset);
				outDesc.m_DepthDesc = ApplyDepthPreset(input.m_DepthPreset);
			});

		return psoCache->GetOrCreate(cached.m_Key, cached.m_Desc);
	}
}
