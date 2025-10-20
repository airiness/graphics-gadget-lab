#include "Precompiled.h"
#include "RenderPassForwardPBR.h"
#include "Application.h"
#include "Renderer.h"
#include "RenderGraph.h"

namespace gglab
{
	RenderPassForwardPBR::RenderPassForwardPBR() noexcept
	{
		auto* renderer = Application::GetInstance()->GetRenderer();
		auto* shaderManager = Application::GetInstance()->GetShaderManager();

		// Shader
		ShaderDesc sd{};
		sd.m_SourcePath = L"Assets/Shaders/Passes/PassForwardPBR.hlsl";
		sd.m_Stage = ShaderStage::Vertex;
		sd.m_Entry = L"VSMain";
		const auto vsId = shaderManager->LoadShader(sd);
		sd.m_Stage = ShaderStage::Pixel;
		sd.m_Entry = L"PSMain";
		const auto psId = shaderManager->LoadShader(sd);

		// KeyInputs
		m_KeyInputs.m_RootSignatureId = renderer->GetCommonRootSignatureId();
		m_KeyInputs.m_InputLayoutId = InputLayoutId::P3N3T2;
		m_KeyInputs.m_VSId = vsId;
		m_KeyInputs.m_PSId = psId;
		m_KeyInputs.m_VSGen = shaderManager->GetGeneration(vsId);
		m_KeyInputs.m_PSGen = shaderManager->GetGeneration(psId);
		m_KeyInputs.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		m_KeyInputs.m_Formats.m_RtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		m_KeyInputs.m_Formats.m_RtvFormats.NumRenderTargets = 1;
		m_KeyInputs.m_Formats.m_DsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		m_KeyInputs.m_Formats.m_SampleCount = 1;
		m_KeyInputs.m_Formats.m_SampleQuality = 0;
		m_KeyInputs.m_RasterizerPreset = RasterizerPreset::Default;
		m_KeyInputs.m_BlendPreset = BlendPreset::Default;
		m_KeyInputs.m_DepthPreset = DepthPreset::Default;
	}

	void RenderPassForwardPBR::AddPass(RenderGraph& rg) noexcept
	{
		struct ForwardPBRData
		{
			RGTextureId m_Depth;
		};

		rg.AddPass<ForwardPBRData>("RenderPassForwardPBR",
			[this](RenderGraph::RGBuilder& builder, ForwardPBRData& data)
			{

			},
			[this, &rg](DX12CommandList* commandList, ForwardPBRData& data)
			{

			});

	}
}

