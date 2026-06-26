#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBLEnvironment.h"
#include "Graphics/Renderer.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGIBLResources.h"
#include "Graphics/RenderGraph/RGDX12ResourceUtils.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"

namespace gglab
{
	namespace
	{
		struct IBLEnvironmentPassParameters
		{
			uint32_t CubemapFaceIndex = 0;
			uint32_t Padding[3]{};
		};
		static_assert(IsPassRootConstantStruct<IBLEnvironmentPassParameters>);
		static_assert(sizeof(IBLEnvironmentPassParameters) == 16);

		struct PassData
		{
			RGTextureId m_EnvironmentCubemap{};
			std::array<RGTextureViewId, CubemapFaceCount> m_Rtvs{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};
	}

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

		rg.AddPass<PassData>("RenderPassIBL.BuildEnvironmentCubemap",
			[renderResRegistry](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);

				auto* envCubemapTexture = renderResRegistry->GetTexture(RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
				GGLAB_ASSERT_NOT_NULL(envCubemapTexture);
	
				data.m_EnvironmentCubemap = builder.Write(iblRes.m_EnvironmentCubemap, RGTextureUsage::RenderTarget);

				const auto rgDesc = ToRGTextureDesc(*envCubemapTexture,
					RGTextureUsage::RenderTarget | RGTextureUsage::Sample);

				for (uint32_t face = 0; face < CubemapFaceCount; ++face)
				{
					const auto rtvDesc = MakeRHITexture2DArrayViewDesc(rgDesc.m_Format, 0, face, 1);
					data.m_Rtvs[face] =
						builder.CreateView<RHITextureViewType::RenderTarget>(data.m_EnvironmentCubemap, rtvDesc);
				}

				data.m_Width = rgDesc.m_Width;
				data.m_Height = rgDesc.m_Height;
			},
			[this, &rg, renderer, renderResRegistry](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandList = executeContext.GetGraphicsCommandList();
				GGLAB_ASSERT_NOT_NULL(commandList);

				auto* texture = rg.GetTexture(data.m_EnvironmentCubemap);
				GGLAB_ASSERT_NOT_NULL(texture);

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
					const auto rtv = executeContext.GetView(data.m_Rtvs[face]);
					commandList->SetRenderTarget(rtv);
					commandList->ClearRenderTarget(rtv, *texture);

					const IBLEnvironmentPassParameters passParameters{
						.CubemapFaceIndex = face,
					};
					commandList->SetGraphicsRoot32BitConstants(
						static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
						passParameters);

					commandList->DrawInstanced(3);
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

			// Pipeline recipe
			m_BaseRecipe.m_RootSignatureId = renderer->GetCommonRootSignatureId();
			m_BaseRecipe.m_InputLayoutId = InputLayoutID::None;
			m_BaseRecipe.m_VSId = vsId;
			m_BaseRecipe.m_PSId = psId;

			m_BaseRecipe.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			m_BaseRecipe.m_Formats.m_RtvFormats.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
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

	DX12PipelineState* RenderPassIBLEnvironment::GetOrCreatePSO(const Renderer & renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_BaseRecipe);
	}
}
