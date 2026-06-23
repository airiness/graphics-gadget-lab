#pragma once
#include "Core/Hash/FNV1a.h"
#include "Graphics/RHI/DX12/Cache/PSOKey.h"
#include "Graphics/RHI/DX12/Cache/PSOCreator.h"

namespace gglab
{
	class DX12Device;
	class DX12PipelineState;
	class DX12PSOCache
	{
	public:
		explicit DX12PSOCache(DX12Device* dx12Device, std::unique_ptr<IPSOCreator> creator) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12PSOCache);
		~DX12PSOCache();

		DX12PipelineState* GetOrCreate(const GraphicsPSOKey& key, const GraphicsPipelineDesc& desc) noexcept;
		DX12PipelineState* GetOrCreate(const ComputePSOKey& key, const ComputePipelineDesc& desc) noexcept;

		void Clear() noexcept;
		void ClearGraphicsPSOCache() noexcept;
		void ClearComputePSOCache() noexcept;
		uint64_t GetRevision() const noexcept
		{
			return m_Revision.load(std::memory_order_relaxed);
		}

	private:
		DX12Device* m_DX12Device = nullptr;
		std::unique_ptr<IPSOCreator> m_Creator;
		std::unordered_map<GraphicsPSOKey, std::unique_ptr<DX12PipelineState>, GraphicsPSOKeyHash> m_GraphicsPSOMap;
		std::unordered_map<ComputePSOKey, std::unique_ptr<DX12PipelineState>, ComputePSOKeyHash> m_ComputePSOMap;

		mutable std::shared_mutex m_Mutex;
		std::atomic_uint64_t m_Revision = 1;
	};
}
