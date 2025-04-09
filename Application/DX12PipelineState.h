#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12PipelineState
	{
	public:
		explicit DX12PipelineState(DX12Device* dx12Device) noexcept;
		virtual ~DX12PipelineState() noexcept;

		ID3D12PipelineState* Get() const noexcept { return m_PipelineState.Get(); }
	private:
		DX12Device* m_DX12Device = nullptr;
		ComPtr<ID3D12PipelineState> m_PipelineState;
	};

	struct DX12GraphicsPipeLineStateDesc
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC m_Desc = {};
	};

	class DX12GraphicsPipelineState : public DX12PipelineState
	{
	public:
		explicit DX12GraphicsPipelineState(DX12Device* dx12Device) noexcept;
		virtual ~DX12GraphicsPipelineState() noexcept;

		void CreateGraphicsPipelineState(const DX12GraphicsPipeLineStateDesc& desc) noexcept;
	};

	struct DX12ComputePipeLineStateDesc
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC m_Desc = {};
	};

	class DX12ComputePipelineState : public DX12PipelineState
	{
	public:
		explicit DX12ComputePipelineState(DX12Device* dx12Device) noexcept;
		virtual ~DX12ComputePipelineState() noexcept;

		void CreateComputePipelineState(const DX12ComputePipeLineStateDesc& desc) noexcept;
	};
}


