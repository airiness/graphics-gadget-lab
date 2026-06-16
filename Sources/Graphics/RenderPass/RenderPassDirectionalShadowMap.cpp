#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassDirectionalShadowMap.h"
#include "Graphics/Renderer.h"
#include "Graphics/AssetManager.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGShadowResources.h"
#include "Graphics/DX12/Cache/InputLayoutLibrary.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorManager.h"

namespace gglab
{
	namespace
	{
		constexpr uint32_t ShadowDepthBiasBitCount = 20;
		constexpr uint32_t ShadowDepthBiasShift = RenderQueueBuilder::VariantBitCount;

		constexpr uint32_t ShadowSlopeBiasBitCount = 16;
		constexpr uint32_t ShadowSlopeBiasShift = ShadowDepthBiasShift + ShadowDepthBiasBitCount;

		constexpr float ShadowSlopeBiasQuantizationScale = 256.0f;

		static_assert(ShadowSlopeBiasShift + ShadowSlopeBiasBitCount <= 64);

		constexpr int32_t SignedFieldMin(uint32_t bitCount) noexcept
		{
			return -static_cast<int32_t>(uint32_t{ 1 } << (bitCount - 1));
		}

		constexpr int32_t SignedFieldMax(uint32_t bitCount) noexcept
		{
			return static_cast<int32_t>((uint32_t{ 1 } << (bitCount - 1)) - 1);
		}

		constexpr uint64_t BitMask(uint32_t bitCount) noexcept
		{
			return (uint64_t{ 1 } << bitCount) - 1ull;
		}

		uint64_t PackSignedVariantField(int32_t value, uint32_t bitCount, uint32_t bitShift) noexcept
		{
			const int32_t clamped = std::clamp(value, SignedFieldMin(bitCount), SignedFieldMax(bitCount));
			return (static_cast<uint64_t>(static_cast<uint32_t>(clamped)) & BitMask(bitCount)) << bitShift;
		}

		uint64_t EncodeShadowRasterizerBiasVariantBits(const DirectionalShadowSettings& settings) noexcept
		{
			const int32_t slopeBias = static_cast<int32_t>(
				std::round(settings.m_RasterizerSlopeScaledDepthBias * ShadowSlopeBiasQuantizationScale));

			return PackSignedVariantField(
				settings.m_RasterizerDepthBias,
				ShadowDepthBiasBitCount,
				ShadowDepthBiasShift) |
				PackSignedVariantField(
					slopeBias,
					ShadowSlopeBiasBitCount,
					ShadowSlopeBiasShift);
		}
	}

	void RenderPassDirectionalShadowMap::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		struct ShadowMapData
		{
			RGTextureId m_ShadowMap{};
			uint32_t m_ShadowMapSize = 0;
		};

		auto* contextPtr = &context;
		GGLAB_ASSERT_NOT_NULL(contextPtr);

		auto* servicesPtr = &services;
		GGLAB_ASSERT_NOT_NULL(servicesPtr);

		EnsureInitialized(services);

		rg.AddPass<ShadowMapData>("RenderPassDirectionalShadowMap",
			[](RenderGraph::RGBuilder& builder, ShadowMapData& data)
			{
				auto& shadowRes = builder.GetBlackboard().Get<RGShadowResources>(ShadowResourcesName);
				data.m_ShadowMap = builder.Write(shadowRes.m_DirectionalShadowMap, RGTextureUsage::DepthStencil);
				data.m_ShadowMapSize = shadowRes.m_ShadowMapSize;
			},
			[this, &rg, contextPtr, servicesPtr](RGExecuteContext& executeContext, ShadowMapData& data)
			{
				auto* commandList = executeContext.m_GraphicsCommandList;
				GGLAB_ASSERT_NOT_NULL(commandList);

				auto* shadowMapTexture = rg.GetTexture(data.m_ShadowMap);
				GGLAB_ASSERT_NOT_NULL(shadowMapTexture);

				const ResourceIndex shadowMapIndex = rg.GetResourceIndex(data.m_ShadowMap);

				D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
				dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
				dsvDesc.Texture2D.MipSlice = 0;

				auto* viewCache = rg.GetViewCache();
				GGLAB_ASSERT_NOT_NULL(viewCache);
				const auto dsv = viewCache->GetOrCreate<ViewType::DSV>(
					shadowMapIndex,
					shadowMapTexture,
					dsvDesc);

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
				commandList->SetViewport(0, 0, data.m_ShadowMapSize, data.m_ShadowMapSize);
				commandList->SetScissorRect(0, 0, data.m_ShadowMapSize, data.m_ShadowMapSize);
				commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				commandList->ClearDepthStencil(dsv, 1.0f, 0);
				commandList->SetRenderTargets(std::span<DX12DescriptorView>{}, dsv);

				commandList->SetGraphicsConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					renderer->GetSceneConstantBuffer()->GetGPUVirtualAddress(contextPtr->m_BackBufferIndex));

				const auto& objectSB = renderer->GetObjectStructuredBuffer();
				commandList->Get()->SetGraphicsRootShaderResourceView(
					static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB),
					objectSB->GetBuffer()->GPUVirtualAddress());

				const auto& materialSB = renderer->GetMaterialStructuredBuffer();
				commandList->Get()->SetGraphicsRootShaderResourceView(
					static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
					materialSB->GetBuffer()->GPUVirtualAddress());

				const auto& viewSB = renderer->GetViewStructuredBuffer();
				commandList->Get()->SetGraphicsRootShaderResourceView(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					viewSB->GetBuffer()->GPUVirtualAddress());

				commandList->SetGraphicsRoot32BitConstant(
					static_cast<uint32_t>(CommonRSRootParamIndex::LocalConstants),
					static_cast<uint32_t>(utils::ToIndex(RenderViewID::DirectionalShadow)),
					static_cast<uint32_t>(CommonLocalConstantIndex::Param1));

				DrawRenderQueue(commandList, *contextPtr, *servicesPtr);
			});
	}

	void RenderPassDirectionalShadowMap::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassDirectionalShadowMap.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			const auto vsId = shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			const auto psId = shaderManager->LoadShader(shaderDesc);

			m_BaseInputs.m_RootSignatureId = renderer->GetCommonRootSignatureId();
			m_BaseInputs.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
			m_BaseInputs.m_VSId = vsId;
			m_BaseInputs.m_PSId = psId;

			m_BaseInputs.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			m_BaseInputs.m_Formats.m_RtvFormats.NumRenderTargets = 0;
			m_BaseInputs.m_Formats.m_DsvFormat = DXGI_FORMAT_D32_FLOAT;
			m_BaseInputs.m_Formats.m_SampleCount = 1;
			m_BaseInputs.m_Formats.m_SampleQuality = 0;

			m_BaseInputs.m_RasterizerPreset = RasterizerPreset::Default;
			m_BaseInputs.m_BlendPreset = BlendPreset::ColorWriteDisable;
			m_BaseInputs.m_DepthPreset = DepthPreset::Default;

			m_IsInitialized = true;
		}

		m_BaseInputs.m_VSGen = shaderManager->GetGeneration(m_BaseInputs.m_VSId);
		m_BaseInputs.m_PSGen = shaderManager->GetGeneration(m_BaseInputs.m_PSId);
	}

	void RenderPassDirectionalShadowMap::DrawRenderQueue(DX12CommandList* commandList,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		const auto& renderQueue = context.GetRenderQueue(RenderViewID::DirectionalShadow);
		if (renderQueue.m_DrawItems.empty())
		{
			return;
		}

		const auto ranges = renderQueue.m_BucketDrawRanges;
		DrawRange(commandList, context, services, renderQueue, ranges[utils::ToIndex(RenderBucket::Opaque)]);
		DrawRange(commandList, context, services, renderQueue, ranges[utils::ToIndex(RenderBucket::AlphaTest)]);
	}

	void RenderPassDirectionalShadowMap::DrawRange(DX12CommandList* commandList,
		const RenderFrameContext& context,
		const RenderServices& services,
		const RenderQueue& renderQueue,
		const DrawItemsRange& range) noexcept
	{
		if (range.m_Count == 0)
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		auto* assetManager = services.m_AssetManager;

		const auto& drawItems = renderQueue.m_DrawItems;

		uint64_t lastVariantBits = std::numeric_limits<uint64_t>::max();
		MeshID lastMeshId{};

		for (uint32_t index = 0; index < range.m_Count; ++index)
		{
			const auto& drawItem = drawItems[range.m_Start + index];

			if (drawItem.m_VariantBits != lastVariantBits)
			{
				auto* pso = GetOrCreatePSOForVariant(
					*renderer,
					drawItem.m_VariantBits,
					context.GetDirectionalShadowSettings());
				commandList->SetPipelineState(*pso);

				lastVariantBits = drawItem.m_VariantBits;
				lastMeshId = {};
			}

			const Mesh* mesh = assetManager->GetMesh(drawItem.m_MeshId);
			if (!mesh || mesh->m_IndexCount == 0 || !mesh->m_IsUploaded)
			{
				GGLAB_LOG_GRAPHICS_WARN("RenderPassDirectionalShadowMap: Mesh is invalid, skip draw item.");
				continue;
			}

			if (drawItem.m_MeshId != lastMeshId)
			{
				D3D12_VERTEX_BUFFER_VIEW vbs[] = { mesh->m_VertexBufferView };
				commandList->SetVertexBuffers(0, vbs);
				commandList->SetIndexBuffer(mesh->m_IndexBufferView);
				lastMeshId = drawItem.m_MeshId;
			}

			commandList->SetGraphicsRoot32BitConstant(
				static_cast<uint32_t>(CommonRSRootParamIndex::LocalConstants),
				drawItem.m_ObjectOffset,
				static_cast<uint32_t>(CommonLocalConstantIndex::Param0));

			commandList->DrawIndexedInstanced(mesh->m_IndexCount);
		}
	}

	DX12PipelineState* RenderPassDirectionalShadowMap::GetOrCreatePSOForVariant(
		const Renderer& renderer,
		uint64_t variantBits,
		const DirectionalShadowSettings& shadowSettings) noexcept
	{
		auto* passRegistry = renderer.GetRenderPassRecipeRegistry();
		GGLAB_ASSERT_NOT_NULL(passRegistry);

		auto* psoCache = renderer.GetPSOCache();
		GGLAB_ASSERT_NOT_NULL(psoCache);

		GraphicsKeyInputs inputs = m_BaseInputs;
		const uint64_t shadowBiasBits = EncodeShadowRasterizerBiasVariantBits(shadowSettings);
		const uint64_t psoVariantBits = variantBits | shadowBiasBits;
		inputs.m_VariantBits = psoVariantBits;
		inputs.m_RasterizerPreset = GetRasterizerPresetFromVariantBits(variantBits);

		const auto psoPassId = MakeVariantPSOPassId("RenderPassDirectionalShadowMap.variant", psoVariantBits);

		const auto& cached = passRegistry->GetOrCreateGraphics(
			psoPassId,
			inputs,
			[&renderer, &shadowSettings](GraphicsPipelineDesc& outDesc, const GraphicsKeyInputs& input, ShaderManager* shaderManager)
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
				outDesc.m_RasterizerDesc.DepthBias = shadowSettings.m_RasterizerDepthBias;
				outDesc.m_RasterizerDesc.SlopeScaledDepthBias = shadowSettings.m_RasterizerSlopeScaledDepthBias;
				outDesc.m_BlendDesc = ApplyBlendPreset(input.m_BlendPreset);
				outDesc.m_DepthDesc = ApplyDepthPreset(input.m_DepthPreset);
			});

		return psoCache->GetOrCreate(cached.m_Key, cached.m_Desc);
	}

	RasterizerPreset RenderPassDirectionalShadowMap::GetRasterizerPresetFromVariantBits(uint64_t variantBits) const noexcept
	{
		const bool doubleSided = RenderQueueBuilder::DecodeVariantDoubleSided(variantBits);
		return doubleSided ? RasterizerPreset::TwoSided : RasterizerPreset::Default;
	}

	std::string RenderPassDirectionalShadowMap::MakeVariantPSOPassId(
		std::string_view baseName,
		uint64_t variantBits) const noexcept
	{
		return std::format("{}.{:016X}", baseName, variantBits);
	}
}
