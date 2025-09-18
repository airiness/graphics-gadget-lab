#include "Precompiled.h"
#include "PSOCreator.h"
#include "DX12Device.h"
#include "Utility.h"

namespace gglab
{
	ComPtr<ID3D12PipelineState> StreamPSOCreator::CreateGraphicsPSO(DX12Device* dx12Device, const GraphicsPipelineDesc& desc)
	{
		ComPtr<ID3D12PipelineState> pso;

		if (!desc.Validate())
		{
			GGLAB_LOG_ERROR("Invalid GraphicsPipelineDesc");
			return nullptr;
		}

		void* subObjects[16]{};
		auto streamDesc = desc.ToStreamDesc(subObjects, std::size(subObjects));
		utility::ThrowIfFailed(dx12Device->Get()->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&pso)));

		return pso;
	}

	ComPtr<ID3D12PipelineState> LibraryPSOCreator::CreateGraphicsPSO(DX12Device* dx12Device, const GraphicsPipelineDesc& desc)
	{
		ComPtr<ID3D12PipelineState> pso;

		if (!desc.Validate())
		{
			GGLAB_LOG_ERROR("Invalid GraphicsPipelineDesc");
			return nullptr;
		}
		return pso;

		// TODO : Management PSO by Library
	}
}
