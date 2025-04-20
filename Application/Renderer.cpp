#include "Precompiled.h"
#include "Renderer.h"
#include "Application.h"
#include "DX12Device.h"
#include "DX12RootSignature.h"
#include "DX12PipelineState.h"
#include "DX12CommandList.h"
#include "DX12SwapChain.h"
#include "DX12Buffer.h"
#include "DX12Texture.h"
#include "DX12Fence.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	Renderer::Renderer() noexcept
	{
		auto width = Application::Get()->GetWindowWidth();
		auto height = Application::Get()->GetWindowHeight();

		m_Device = std::make_unique<DX12Device>();
		m_Device->Initialize();
	}

	Renderer::~Renderer() noexcept
	{
	}

	void Renderer::Initialize() noexcept
	{
		InitializeRootSignatures();
		InitializePipelineStates();
	}

	void Renderer::Update() noexcept
	{
	}

	void Renderer::Render() noexcept
	{
		auto swapChain = m_Device->GetSwapChain();
		auto cbvDescriptorHeap = m_Device->GetCbvSrvUavDescriptorHeap();
		auto backBufferIndex = swapChain->GetCurrentBackBufferIndex();
		auto bufferWidth = swapChain->GetBufferWidth();
		auto bufferHeight = swapChain->GetBufferHeight();

		auto commandList = m_Device->GetGraphicsCommandList(backBufferIndex);
		commandList->Begin();

		commandList->SetGraphicsRootSignature(m_RootSignatures.at(static_cast<uint32_t>(RootSignatureIndex::CommonRootSignature)).get());
		commandList->SetDescriptorHeap(cbvDescriptorHeap);

		commandList->SetViewport(0, 0, bufferWidth, bufferHeight);
		commandList->SetScissorRect(0, 0, bufferWidth, bufferHeight);

		commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		// TODO: raw Command
		

		// TODO: Management Model Info in another place

	}

	void Renderer::Finalize() noexcept
	{
	}

	void Renderer::InitializeRootSignatures() noexcept
	{
		// Common RootSignature
		{
			CD3DX12_DESCRIPTOR_RANGE1 range = {};
			range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

			CD3DX12_ROOT_PARAMETER1 rootParameters[(uint32_t)CommonRSRootParamIndex::RootParamCount] = {};
			rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::ConstantBufferIndex)].InitAsConstantBufferView(0);
			rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::TextureDescriptorTable)].InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_PIXEL);

			CD3DX12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
			staticSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);


			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
			rootSignatureDesc.Init_1_1(
				static_cast<uint32_t>(CommonRSRootParamIndex::RootParamCount), rootParameters,
				1, staticSamplers,
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

			m_RootSignatures[static_cast<uint32_t>(RootSignatureIndex::CommonRootSignature)] = std::make_unique<DX12RootSignature>(m_Device.get(), rootSignatureDesc);
		}

	}

	void Renderer::InitializePipelineStates() noexcept
	{
		// Normal Texture Model PSO
		{
			ComPtr<ID3DBlob> vertexShaderBlob;
			utility::ThrowIfFailed(D3DReadFileToBlob(L"TexturedModelVS.cso", &vertexShaderBlob));

			ComPtr<ID3DBlob> pixelShaderBlob;
			utility::ThrowIfFailed(D3DReadFileToBlob(L"TexturedModelPS.cso", &pixelShaderBlob));

			// Input Layout
			D3D12_INPUT_ELEMENT_DESC inputLayout[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, m_Position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, m_Normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, m_TexCoord), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			};
			D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
			inputLayoutDesc.pInputElementDescs = inputLayout;
			inputLayoutDesc.NumElements = ARRAYSIZE(inputLayout);

			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.InputLayout = inputLayoutDesc;
			psoDesc.pRootSignature = m_RootSignatures[static_cast<uint32_t>(RootSignatureIndex::CommonRootSignature)]->Get();
			psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
			psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
			psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC2(D3D12_DEFAULT);
			psoDesc.SampleMask = UINT_MAX;
			psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			psoDesc.NumRenderTargets = 1;
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			psoDesc.SampleDesc.Count = 1;
			psoDesc.SampleDesc.Quality = 0;

			DX12GraphicsPipelineStateDesc graphicsPSODesc = {};
			graphicsPSODesc.m_Desc = psoDesc;

			m_PipelineStates[static_cast<uint32_t>(PSOIndex::TexturedModelPSO)] = std::make_unique<DX12GraphicsPipelineState>(m_Device.get(), graphicsPSODesc);

		}
	}

	void Renderer::InitializeRenderTargets() noexcept
	{
		auto app = Application::Get();
		auto width = app->GetWindowWidth();
		auto height = app->GetWindowHeight();

		// RenderTargets
		for (uint32_t i = 0; i < static_cast<uint32_t>(RenderTargetIndex::DS0); ++i)
		{
			auto rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			auto rtResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(rtFormat, width, height);
			const float rtClearColor[] = { 0.0f,0.4f,0.5f,1.0f };
			auto rtClearValue = CD3DX12_CLEAR_VALUE(rtFormat, rtClearColor);

			mRenderTargets[i] = std::make_unique<DX12Texture>(m_Device.get(),
				D3D12_HEAP_TYPE_DEFAULT,
				rtResourceDesc,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				rtClearValue);
		}

		// DepthStencil
		{
			auto dsFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			auto dsResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(dsFormat, width, height);
			auto dsClearValue = CD3DX12_CLEAR_VALUE(dsFormat, 1.0f, 0);

			mRenderTargets[static_cast<uint32_t>(RenderTargetIndex::DS0)] = std::make_unique<DX12Texture>(m_Device.get(),
				D3D12_HEAP_TYPE_DEFAULT,
				dsResourceDesc,
				D3D12_RESOURCE_STATE_DEPTH_WRITE,
				dsClearValue);
		}
	}
	void Renderer::InitializeConstantBuffer() noexcept
	{
		m_GlobalConstantBuffer = std::make_unique<DX12ConstantBuffer<GlobalConstantBuffer>>(m_Device.get());

	}
	void Renderer::InitializeSyncObjects() noexcept
	{
		m_Fence = std::make_unique<DX12Fence>(m_Device.get());

	}
	void Renderer::UpdateGpuBuffers() noexcept
	{
		UpdateGlobalConstantBuffer();

	}
	void Renderer::UpdateGlobalConstantBuffer() noexcept
	{
	}
}