#include "Precompiled.h"
#include "RenderPassTexColor.h"
#include "RenderGraph.h"
#include "DX12Device.h"
#include "DX12RootSignature.h"
#include "DX12PipelineState.h"
#include "VertexData.h"
#include "Application.h"
#include "Renderer.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	RenderPassTexColor::RenderPassTexColor(DX12Device* dx12Device) noexcept : 
		m_DX12Device(dx12Device)
	{
		InitializePSO();
	}

	void RenderPassTexColor::AddPass(RenderGraph& rg) noexcept
	{
		struct TexColorData {};

		const auto& texColorPass = rg.AddPass<TexColorData>("RenderPassTexColor",
			[](RenderGraph::RGBuilder& builder, TexColorData& data)
			{
				builder.SideEffect();
			},
			[this](DX12CommandList* commandList, TexColorData& data)
			{
				auto* swapChain = m_DX12Device->GetSwapChain();


			});
	}

	void RenderPassTexColor::InitializePSO() noexcept
	{
		auto* renderer = Application::GetInstance()->GetRenderer();
		auto* commonRootSignature = renderer->GetCommonRootSignature();

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

		CD3DX12_RASTERIZER_DESC rasterizerDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
		//rasterizerDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;	// Wireframe mode for debugging
		rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = inputLayoutDesc;
		psoDesc.pRootSignature = commonRootSignature->Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
		psoDesc.RasterizerState = rasterizerDesc;
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

		m_PSO = std::make_unique<DX12GraphicsPipelineState>(m_DX12Device, graphicsPSODesc);
	}
}


