#pragma once
namespace gglab
{
	class DX12Device;
	class DX12PipelineState
	{
	public:
		explicit DX12PipelineState(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12PipelineState);
		virtual ~DX12PipelineState() = default;

		ID3D12PipelineState* Get() const noexcept { return m_PipelineState.Get(); }
	protected:
		DX12Device* m_DX12Device = nullptr;
		ComPtr<ID3D12PipelineState> m_PipelineState;
	};

	struct DX12GraphicsPipelineStateDesc
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC m_Desc = {};
	};

	class DX12GraphicsPipelineState : public DX12PipelineState
	{
	public:
		explicit DX12GraphicsPipelineState(DX12Device* dx12Device, const DX12GraphicsPipelineStateDesc& desc) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12GraphicsPipelineState);
		~DX12GraphicsPipelineState() override = default;

	private:
		void CreateGraphicsPipelineState() noexcept;

	private:
		DX12GraphicsPipelineStateDesc m_GraphicsPSODesc;
	};

	struct DX12ComputePipeLineStateDesc
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC m_Desc = {};
	};

	class DX12ComputePipelineState : public DX12PipelineState
	{
	public:
		explicit DX12ComputePipelineState(DX12Device* dx12Device, const DX12ComputePipeLineStateDesc& desc) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12ComputePipelineState);
		~DX12ComputePipelineState() override = default;

	private:
		void CreateComputePipelineState() noexcept;

	private:
		DX12ComputePipeLineStateDesc m_ComputePSODesc;
	};
}


