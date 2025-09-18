#pragma once
#include "PipelineDesc.h"
namespace gglab
{
	class DX12Device;
	class IPSOCreator
	{
	public:
		virtual ~IPSOCreator() = default;

		virtual ComPtr<ID3D12PipelineState> CreateGraphicsPSO(DX12Device* dx12Device, const GraphicsPipelineDesc& desc) = 0;
	};

	class StreamPSOCreator final : public IPSOCreator
	{
	public:
		ComPtr<ID3D12PipelineState> CreateGraphicsPSO(DX12Device* dx12Device, const GraphicsPipelineDesc& desc) override;
	};

	class LibraryPSOCreator final : public IPSOCreator
	{
	public:
		ComPtr<ID3D12PipelineState> CreateGraphicsPSO(DX12Device* dx12Device, const GraphicsPipelineDesc& desc) override;

	//private:
	//	static std::wstring MakePipelineName(const PSOKey& key);

	private:
		ID3D12PipelineLibrary* m_PipelineLibrary = nullptr;
	};
}