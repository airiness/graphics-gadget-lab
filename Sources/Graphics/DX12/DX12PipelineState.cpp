#include "Core/Precompiled.h"
#include "Graphics/DX12/DX12PipelineState.h"
#include "Graphics/DX12/DX12Device.h"
#include "Core/HResult.h"

namespace gglab
{
	DX12PipelineState::DX12PipelineState(ComPtr<ID3D12PipelineState> pipelineState) noexcept :
		m_PipelineState(pipelineState)
	{
	}

	DX12PipelineState::DX12PipelineState(DX12Device* dx12Device, const D3D12_PIPELINE_STATE_STREAM_DESC& streamDesc) noexcept
	{
		GGLAB_HR_DX(dx12Device->Get()->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PipelineState)), dx12Device->Get());
	}
}
