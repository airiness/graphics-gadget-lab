#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12PipelineState
	{
	public:
		explicit DX12PipelineState(DX12Device* dx12Device) noexcept;

		~DX12PipelineState() noexcept;
		void Initialize() noexcept;
		void Finalize() noexcept;


	private:
		DX12Device* m_DX12Device = nullptr;
		ComPtr<ID3D12PipelineState> m_PipelineState;
		ComPtr<ID3D12RootSignature> m_RootSignature;
		std::vector<ComPtr<ID3DBlob>> m_ShaderBlobs;
		std::vector<ComPtr<ID3DBlob>> m_ShaderErrors;
	};

	struct DX12GraphicsPipeLineStateDesc
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC m_Desc = {};
	};

	struct DX12ComputePipeLineStateDesc
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC m_Desc = {};
	};

}


