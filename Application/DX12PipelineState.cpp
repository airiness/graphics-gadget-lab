#include "Precompiled.h"
#include "DX12PipelineState.h"
#include "DX12Device.h"
#include "Utility.h"

namespace gglab
{
	DX12PipelineState::DX12PipelineState(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device)
	{
	}

	DX12PipelineState::~DX12PipelineState() noexcept
	{
	}

	DX12GraphicsPipelineState::DX12GraphicsPipelineState(DX12Device* dx12Device, const DX12GraphicsPipelineStateDesc& desc) noexcept :
		DX12PipelineState(dx12Device),
		m_GraphicsPSODesc(desc)
	{
		CreateGraphicsPipelineState();
	}

	DX12GraphicsPipelineState::~DX12GraphicsPipelineState() noexcept
	{
	}

	void DX12GraphicsPipelineState::CreateGraphicsPipelineState() noexcept
	{
		auto device = m_DX12Device->Get();
		utility::ThrowIfFailed(device->CreateGraphicsPipelineState(&m_GraphicsPSODesc.m_Desc, IID_PPV_ARGS(&m_PipelineState)));

		// TODO: debug name
	}

	DX12ComputePipelineState::DX12ComputePipelineState(DX12Device* dx12Device, const DX12ComputePipeLineStateDesc& desc) noexcept :
		DX12PipelineState(dx12Device),
		m_ComputePSODesc(desc)
	{
		CreateComputePipelineState();
	}

	DX12ComputePipelineState::~DX12ComputePipelineState() noexcept
	{
	}

	void DX12ComputePipelineState::CreateComputePipelineState() noexcept
	{
		auto device = m_DX12Device->Get();
		utility::ThrowIfFailed(device->CreateComputePipelineState(&m_ComputePSODesc.m_Desc, IID_PPV_ARGS(&m_PipelineState)));

		// TODO: debug name
	}

} // namespace gglab
