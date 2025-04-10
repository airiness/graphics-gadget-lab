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
			CD3DX12_ROOT_PARAMETER1 rootParameters[(uint32_t)CommonRSRootParamIndex::RootParamCount];
			rootParameters[(uint32_t)CommonRSRootParamIndex::ConstantBufferIndex].InitAsConstantBufferView(0);

			CD3DX12_STATIC_SAMPLER_DESC staticSamplers[1];
			staticSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
			rootSignatureDesc.Init_1_1(
				static_cast<uint32_t>(CommonRSRootParamIndex::RootParamCount), rootParameters, 
				1, staticSamplers, 
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

			ComPtr<ID3DBlob> signature;
			ComPtr<ID3DBlob> error;

			utility::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error));

			utility::ThrowIfFailed(m_Device->Get()->CreateRootSignature(
				0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_RootSignatures[static_cast<uint32_t>(RootSignatureIndex::CommonRootSignature)]->Get())));
		}

	}

	void Renderer::InitializePipelineStates() noexcept
	{
	}
}