#include "Precompiled.h"
#include "Renderer.h"
#include "Application.h"
#include "DX12Device.h"
#include "DX12RootSignature.h"
#include "DX12CommandList.h"
#include "DX12CommandQueue.h"
#include "DX12SwapChain.h"
#include "DX12CommandAllocator.h"
#include "DX12ConstantBuffer.h"
#include "RenderGraph.h"
#include "RGGpuResourceAllocator.h"
#include "AssetManager.h"
#include "Components.h"
#include "Camera.h"
#include "RenderView.h"
#include "RenderScene.h"
#include "RenderPassForwardPBR.h"
#include "MathUtils.h"

namespace gglab
{
	Renderer::Renderer() noexcept
	{
		m_Device = std::make_unique<DX12Device>();
		m_Device->Initialize();
	}

	void Renderer::Initialize() noexcept
	{
		m_RGGpuAllocator = std::make_unique<RGGpuResourceAllocator>(m_Device.get());

		DX12ViewCache::DescriptorsAllocatorArray descriptorAllocators =
		{
			m_Device->GetRtvDescriptorAllocator(),
			m_Device->GetDsvDescriptorAllocator(),
			m_Device->GetCbvSrvUavDescriptorAllocator()
		};
		m_ViewCache = std::make_unique<DX12ViewCache>(m_Device.get(), descriptorAllocators);
		m_PSOCache = std::make_unique<DX12PSOCache>(m_Device.get(), std::make_unique<StreamPSOCreator>());
		m_RootSignatureCache = std::make_unique<DX12RootSignatureCache>(m_Device.get());
		m_RenderPassRecipeRegistry = std::make_unique<RenderPassRecipeRegistry>(Application::GetInstance()->GetShaderManager());

		CreateCommonRootSignature();
		InitializeGpuBuffers();

		m_IsInitialized = true;
	}

	void Renderer::Render(RenderGraph& rg, const RenderFrameContext& renderContext) noexcept
	{
		if (m_IsInitialized == false)
		{
			return;
		}

		auto swapChain = m_Device->GetSwapChain();
		auto backBufferIndex = swapChain->GetCurrentBackBufferIndex();
		auto commandAllocatorPool = m_Device->GetGraphicsCommandAllocatorPool();
		auto commandList = m_Device->GetGraphicsCommandList(backBufferIndex);
		auto commandQueue = m_Device->GetGraphicsCommandQueue();

		// Wait Structured Buffer upload
		if (renderContext.m_UploadFencePoint.IsValid())
		{
			commandQueue->Wait(renderContext.m_UploadFencePoint);
		}

		swapChain->WaitFrameCompletion();

		auto commandAllocator = commandAllocatorPool->RequestCommandAllocator();
		commandList->Begin(commandAllocator);

		RGExecuteContext executeContext = {};
		executeContext.m_GraphicsCommandList = commandList;
		rg.Execute(executeContext);

		commandList->End();

		const DX12CommandList* commandLists[] = { commandList };
		auto fencePoint = commandQueue->Execute(commandLists);

		rg.Retire(fencePoint);

		m_RGGpuAllocator->Tick();

		commandAllocatorPool->RecycleCommandAllocator(commandAllocator, fencePoint);

		swapChain->UpdateFrameSyncObject(std::move(fencePoint));
		swapChain->Present();

	}

	void Renderer::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		m_IsInitialized = false;

		m_Device->FlushGPU();
	}

	DX12RootSignature* Renderer::GetCommonRootSignature() const noexcept
	{
		return m_RootSignatureCache->GetDX12RootSignature(m_CommonRootSignatureId);
	}

	void Renderer::UpdateFrameConstants(const RenderView& view, const RenderScene& scene) noexcept
	{
		// Update FrameCB
		FrameCBData frameCB{};
		frameCB.ViewMat = view.m_View;
		frameCB.ProjMat = view.m_Proj;
		frameCB.CameraPos = utils::ToVector4(view.m_CameraPosition, 1.0f);
		frameCB.Exposure = 1.0f;	// TODO: camera exposure

		frameCB.ObjectBaseIndex = scene.m_ObjectBaseIndex;
		frameCB.MaterialBaseIndex = scene.m_MaterialBaseIndex;

		frameCB.MainLight = scene.m_MainLight;

		const auto currentIndex = m_Device->GetSwapChain()->GetCurrentBackBufferIndex();
		m_FrameCB->Update(frameCB, currentIndex);
	}

	RenderGraph::CreateInfo Renderer::CreateRenderGraphCreateInfo() const noexcept
	{
		RenderGraph::CreateInfo rgCreateInfo{};
		rgCreateInfo.m_GpuResourceAllocator = m_RGGpuAllocator.get();
		rgCreateInfo.m_ViewCache = m_ViewCache.get();

		return rgCreateInfo;
	}

	uint32_t Renderer::GetCurrentBackBufferIndex() const noexcept
	{
		return m_Device->GetSwapChain()->GetCurrentBackBufferIndex();
	}

	void Renderer::CreateCommonRootSignature() noexcept
	{
		CD3DX12_DESCRIPTOR_RANGE1 range = {};
		range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			1, // numDescriptors: t0
			0, // baseShaderRegister
			0, // registerSpace
			D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::RootParamCount)] = {};

		// b0: FrameCB
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::FrameCB)].InitAsConstantBufferView(0);

		// b1: ObjectCB, num32BitValues: 1, shaderRegister: b1
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::ObjectCB)].InitAsConstants(1, 1);

		// t1: ObjectSB
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB)].InitAsShaderResourceView(1);

		// t2: materialSB
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB)].InitAsShaderResourceView(2);

		// t0: BaseColorTex descriptor table
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::TextureDescriptorTable)]
			.InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_PIXEL);

		constexpr uint32_t StaticSamplerCount = 1;
		CD3DX12_STATIC_SAMPLER_DESC staticSamplers[StaticSamplerCount] = {};
		staticSamplers[0].Init(
			0,	// register s0
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.Init_1_1(
			static_cast<uint32_t>(CommonRSRootParamIndex::RootParamCount), rootParameters,
			StaticSamplerCount, staticSamplers,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

		auto [id, rootSig] = m_RootSignatureCache->GetOrCreate(rootSignatureDesc);
		m_CommonRootSignatureId = id;
	}

	void Renderer::InitializeGpuBuffers() noexcept
	{
		// Initialize global constant buffer
		{
			const auto constantBufferFrames = m_Device->GetBufferCount();
			m_FrameCB = std::make_unique<DX12ConstantBuffer<FrameCBData>>(m_Device.get(), constantBufferFrames);
		}

		// Initialize structured buffers objects and materials
		{
			m_ObjectSB = std::make_unique<DX12RingStructuredBuffer<ObjectGPU>>(m_Device.get(), MaxObjectCapacity); // max element count
			m_MaterialSB = std::make_unique<DX12RingStructuredBuffer<MaterialGPU>>(m_Device.get(), MaxMaterialCapacity); // max element count
		}
	}
}