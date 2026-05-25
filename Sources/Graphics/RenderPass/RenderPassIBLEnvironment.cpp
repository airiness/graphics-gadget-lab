#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBLEnvironment.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGIBLResources.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/DX12/Cache/InputLayoutLibrary.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/Utility/TextureViewDescUtils.h"

namespace gglab
{
	void RenderPassIBLEnvironment::AddPass(RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(context);

		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* renderResRegistry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);

		const auto shouldBuild = renderResRegistry->IsDirty(RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);

		if (!shouldBuild)
		{
			return;
		}

		EnsureInitialized(services);

		struct BuildPassData
		{
			RGTextureId m_EnvironmentCubemap;
			std::array<ViewKey, CubemapFaceCount> m_RtvKeys;

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};

		rg.AddPass<BuildPassData>("RenderPassIBL.BuildEnvironmentCubemap",
			[renderResRegistry](RenderGraph::RGBuilder& builder, BuildPassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);

				auto* envCubemapTexture = renderResRegistry->GetTexture(RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
				GGLAB_ASSERT_NOT_NULL(envCubemapTexture);
	
				data.m_EnvironmentCubemap = builder.Write(iblRes.m_EnvironmentCubemap, RGTextureUsage::RenderTarget);

				const auto rgDesc = ToRGTextureDesc(*envCubemapTexture,
					RGTextureUsage::RenderTarget | RGTextureUsage::Sample);

				const auto externalIndex = renderResRegistry->GetExternalIndex(RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
				GGLAB_ASSERT_MSG(ExternalResourceIndex::IsExternal(externalIndex),
					"IBL Environment Cubemap must be imported as an external RenderGraph resource.");

				for (uint32_t face = 0; face < CubemapFaceCount; ++face)
				{
					auto rtvDesc = MakeTexture2DArrayRtvDesc(rgDesc.m_Format, 0, face, 1);

					data.m_RtvKeys[face] = DX12ViewCache::BuildKey<ViewType::RTV>(
						externalIndex,
						envCubemapTexture,
						rtvDesc);
				}

				data.m_Width = rgDesc.m_Width;
				data.m_Height = rgDesc.m_Height;
			},
			[this, &rg, renderer, renderResRegistry](RGExecuteContext& executeContext, BuildPassData& data)
			{
				auto* commandList = executeContext.m_GraphicsCommandList;
				GGLAB_ASSERT_NOT_NULL(commandList);

				auto* texture = rg.GetTexture(data.m_EnvironmentCubemap);
				GGLAB_ASSERT_NOT_NULL(texture);

				auto* viewCache = rg.GetViewCache();
				GGLAB_ASSERT_NOT_NULL(viewCache);

				// Temporary manual barrier.
				// TODO: Remove this after RenderGraph supports automatic barrier inference from builder.Write/Read.
				{
					CD3DX12_TEXTURE_BARRIER toRenderTarget(
						D3D12_BARRIER_SYNC_ALL,
						D3D12_BARRIER_SYNC_RENDER_TARGET,
						D3D12_BARRIER_ACCESS_COMMON,
						D3D12_BARRIER_ACCESS_RENDER_TARGET,
						D3D12_BARRIER_LAYOUT_COMMON,
						D3D12_BARRIER_LAYOUT_RENDER_TARGET,
						texture->Get(),
						CD3DX12_BARRIER_SUBRESOURCE_RANGE(0, 1, 0, CubemapFaceCount));

					commandList->AddTextureBarrier(toRenderTarget);
					commandList->FlushBarriers();
				}

				auto* pso = GetOrCreatePSO(*renderer);
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
				commandList->SetViewport(0, 0, data.m_Width, data.m_Height);
				commandList->SetScissorRect(0, 0, data.m_Width, data.m_Height);
				commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				for (uint32_t face = 0; face < CubemapFaceCount; ++face)
				{
					const auto rtv = viewCache->GetOrCreate(data.m_RtvKeys[face], texture);
					commandList->SetRenderTarget(rtv);
					commandList->ClearRenderTarget(rtv, *texture);

					commandList->Get()->SetGraphicsRoot32BitConstant(
						static_cast<uint32_t>(CommonRSRootParamIndex::DrawCB),
						face,
						static_cast<uint32_t>(CommonDrawCBIndex::DrawParam0));

					commandList->DrawInstanced(3);
				}

				// Resource Barrier
				{
					CD3DX12_TEXTURE_BARRIER toCommon(
						D3D12_BARRIER_SYNC_RENDER_TARGET,
						D3D12_BARRIER_SYNC_ALL,
						D3D12_BARRIER_ACCESS_RENDER_TARGET,
						D3D12_BARRIER_ACCESS_COMMON,
						D3D12_BARRIER_LAYOUT_RENDER_TARGET,
						D3D12_BARRIER_LAYOUT_COMMON,
						texture->Get(),
						CD3DX12_BARRIER_SUBRESOURCE_RANGE(0, 1, 0, CubemapFaceCount));

					commandList->AddTextureBarrier(toCommon);
					commandList->FlushBarriers();
				}

				renderResRegistry->ClearDirty(RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
			});

	}
	void RenderPassIBLEnvironment::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			// Shader
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassIBLEnvironment.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			const auto vsId = shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			const auto psId = shaderManager->LoadShader(shaderDesc);

			// KeyInputs
			m_BaseInputs.m_RootSignatureId = renderer->GetCommonRootSignatureId();
			m_BaseInputs.m_InputLayoutId = InputLayoutID::None;
			m_BaseInputs.m_VSId = vsId;
			m_BaseInputs.m_PSId = psId;

			m_BaseInputs.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			m_BaseInputs.m_Formats.m_RtvFormats.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
			m_BaseInputs.m_Formats.m_RtvFormats.NumRenderTargets = 1;
			m_BaseInputs.m_Formats.m_DsvFormat = DXGI_FORMAT_UNKNOWN;
			m_BaseInputs.m_Formats.m_SampleCount = 1;
			m_BaseInputs.m_Formats.m_SampleQuality = 0;

			m_BaseInputs.m_RasterizerPreset = RasterizerPreset::Default;
			m_BaseInputs.m_BlendPreset = BlendPreset::Default;
			m_BaseInputs.m_DepthPreset = DepthPreset::DepthDisabled;

			m_IsInitialized = true;
		}

		m_BaseInputs.m_VSGen = shaderManager->GetGeneration(m_BaseInputs.m_VSId);
		m_BaseInputs.m_PSGen = shaderManager->GetGeneration(m_BaseInputs.m_PSId);
	}

	DX12PipelineState* RenderPassIBLEnvironment::GetOrCreatePSO(const Renderer & renderer) noexcept
	{
		auto* passRegistry = renderer.GetRenderPassRecipeRegistry();
		GGLAB_ASSERT_NOT_NULL(passRegistry);

		auto* psoCache = renderer.GetPSOCache();
		GGLAB_ASSERT_NOT_NULL(psoCache);

		GraphicsKeyInputs inputs = m_BaseInputs;
		const auto& cached = passRegistry->GetOrCreateGraphics("RenderPassIBLEnvironment",
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
