#pragma once
namespace graphicsGadgetLab
{
	class DX12PipelineState
	{
	public:
		DX12PipelineState() noexcept;
		DX12PipelineState(const DX12PipelineState&) = delete;
		DX12PipelineState& operator=(const DX12PipelineState&) = delete;
		DX12PipelineState(DX12PipelineState&&) = delete;
		DX12PipelineState& operator=(DX12PipelineState&&) = delete;
		~DX12PipelineState() noexcept;
		void Initialize() noexcept;
		void Finalize() noexcept;


	private:
		ComPtr<ID3D12PipelineState> m_PipelineState;
		ComPtr<ID3D12RootSignature> m_RootSignature;
		std::vector<ComPtr<ID3DBlob>> m_ShaderBlobs;
		std::vector<ComPtr<ID3DBlob>> m_ShaderErrors;
		std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputElements;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC m_PipelineStateDesc = {};
		D3D12_ROOT_SIGNATURE_DESC m_RootSignatureDesc = {};
		bool m_Initialized = false;

	};


}


