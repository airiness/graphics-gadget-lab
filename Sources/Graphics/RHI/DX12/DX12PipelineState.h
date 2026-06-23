#pragma once
#include "Core/Platform/Win/ComTypes.h"
namespace gglab
{
	class DX12Device;
	class DX12PipelineState
	{
	public:
		explicit DX12PipelineState(ComPtr<ID3D12PipelineState> pipelineState) noexcept;
		explicit DX12PipelineState(DX12Device* dx12Device, const D3D12_PIPELINE_STATE_STREAM_DESC& streamDesc) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12PipelineState);
		virtual ~DX12PipelineState() = default;

		ID3D12PipelineState* Get() const noexcept { return m_PipelineState.Get(); }

	private:
		ComPtr<ID3D12PipelineState> m_PipelineState;
	};
}


