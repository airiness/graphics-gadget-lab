#include "Precompiled.h"
#include "Renderer.h"
#include "Application.h"
#include "DX12Device.h"
#include "DX12RootSignature.h"
#include "DX12PipelineState.h"
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
	}

	void Renderer::Finalize() noexcept
	{
	}

	void Renderer::InitializeRootSignatures() noexcept
	{
		// Common RootSignature
		{
			CD3DX12_ROOT_PARAMETER1 rootParameters[(uint32_t)CommonRSRootParamIndex::RootParamCount] = {};
			rootParameters[(uint32_t)CommonRSRootParamIndex::ConstantBufferIndex].InitAsConstantBufferView(0);

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
			utility::ThrowIfFailed(D3DReadFileToBlob(L"TexturedModelPS.cso", &vertexShaderBlob));

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

			D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPSODesc = {};


			//DX12GraphicsPipelineStateDesc graphicsPSODesc = {};
			//graphicsPSODesc.m_Desc.pRootSignature = m_RootSignatures[static_cast<uint32_t>(RootSignatureIndex::CommonRootSignature)]->Get();
			//graphicsPSODesc.m_Desc.VS = { nullptr, 0 };
			//graphicsPSODesc.m_Desc.PS = { nullptr, 0 };
			//graphicsPSODesc.m_Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			//graphicsPSODesc.m_Desc.NumRenderTargets = 1;
			//graphicsPSODesc.m_Desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			//graphicsPSODesc.m_Desc.SampleMask = UINT_MAX;
			//graphicsPSODesc.m_Desc.SampleDescription.Count = 1;
			//graphicsPSODesc.m_Desc.SampleDescription.Quality = 0;
			//m_PipelineStates[static_cast<uint32_t>(PSOIndex::NormalTexturePSO)] = std::make_unique<DX12GraphicsPipelineState>(m_Device.get(), graphicsPSODesc);
		}


	}
}