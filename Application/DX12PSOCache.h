#pragma once
#include "FNV1a.h"
#include "TypedIndex.h"
#include "PSOKey.h"
#include "PSOCreator.h"

namespace gglab
{
	class DX12Device;
	class DX12PSOCache
	{
	public:
		explicit DX12PSOCache(DX12Device* dx12Device, std::unique_ptr<IPSOCreator> creator) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12PSOCache);
		~DX12PSOCache();

		ID3D12PipelineState* GetOrCreate(const GraphicsPSOKey& key, const GraphicsPipelineDesc& desc) noexcept;
		ID3D12PipelineState* GetOrCreate(const ComputePSOKey& key, const ComputePipelineDesc& desc) noexcept;

		void Clear() noexcept;
		void ClearGraphicsPSOCache() noexcept;
		void ClearComputePSOCache() noexcept;
	private:
		DX12Device* m_DX12Device = nullptr;
		std::unique_ptr<IPSOCreator> m_Creator;
		std::unordered_map<GraphicsPSOKey, ComPtr<ID3D12PipelineState>, GraphicsPSOKeyHash> m_GraphicsPSOMap;
		std::unordered_map<ComputePSOKey, ComPtr<ID3D12PipelineState>, ComputePSOKeyHash> m_ComputePSOMap;

		mutable std::shared_mutex m_Mutex;
	};
}