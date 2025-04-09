#include "Precompiled.h"
#include "DX12PipelineState.h"

namespace graphicsGadgetLab
{
	DX12PipelineState::DX12PipelineState(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device)
	{
	}

	DX12PipelineState::~DX12PipelineState() noexcept
	{
	}

	DX12GraphicsPipelineState::DX12GraphicsPipelineState(DX12Device* dx12Device) noexcept :
		DX12PipelineState(dx12Device)
	{
	}

	DX12GraphicsPipelineState::~DX12GraphicsPipelineState() noexcept
	{
	}

	void DX12GraphicsPipelineState::CreateGraphicsPipelineState(const DX12GraphicsPipeLineStateDesc& desc) noexcept
	{
	}

	DX12ComputePipelineState::DX12ComputePipelineState(DX12Device* dx12Device) noexcept :
		DX12PipelineState(dx12Device)
	{
	}

	DX12ComputePipelineState::~DX12ComputePipelineState() noexcept
	{
	}

	void DX12ComputePipelineState::CreateComputePipelineState(const DX12ComputePipeLineStateDesc& desc) noexcept
	{
	}

} // namespace graphicsGadgetLab
