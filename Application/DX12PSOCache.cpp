#include "Precompiled.h"
#include "DX12PSOCache.h"

namespace gglab
{
	DX12PSOCache::DX12PSOCache(DX12Device* dx12Device, std::unique_ptr<IPSOCreator> creator) noexcept :
		m_DX12Device(dx12Device),
		m_Creator(std::move(creator))
	{
	}

	DX12PSOCache::~DX12PSOCache() = default;

	ID3D12PipelineState* DX12PSOCache::GetOrCreate(const GraphicsPSOKey& key, const GraphicsPipelineDesc& desc) noexcept
	{
		{
			std::shared_lock lock(m_Mutex);
			auto it = m_GraphicsPSOMap.find(key);
			if (it != m_GraphicsPSOMap.end())
			{
				return it->second.Get();
			}
		}

		std::unique_lock lock(m_Mutex);
		auto it = m_GraphicsPSOMap.find(key);
		if (it != m_GraphicsPSOMap.end())
		{
			return it->second.Get();
		}

		GGLAB_ASSERT_MSG(m_Creator, "PSO Creator is null");
		auto pso = m_Creator->CreateGraphicsPSO(m_DX12Device, desc);
		GGLAB_ASSERT_MSG(pso, "Failed to create PSO");

		auto& slot = m_GraphicsPSOMap[key];
		slot = std::move(pso);
		return slot.Get();
	}

	ID3D12PipelineState* DX12PSOCache::GetOrCreate(const ComputePSOKey& key, const ComputePipelineDesc& desc) noexcept
	{
		{
			std::shared_lock lock(m_Mutex);
			auto it = m_ComputePSOMap.find(key);
			if (it != m_ComputePSOMap.end())
			{
				return it->second.Get();
			}
		}

		std::unique_lock lock(m_Mutex);
		auto it = m_ComputePSOMap.find(key);
		if (it != m_ComputePSOMap.end())
		{
			return it->second.Get();
		}
		GGLAB_ASSERT_MSG(m_Creator, "PSO Creator is null");
		auto pso = m_Creator->CreateComputePSO(m_DX12Device, desc);
		GGLAB_ASSERT_MSG(pso, "Failed to create PSO");
		auto& slot = m_ComputePSOMap[key];
		slot = std::move(pso);
		return slot.Get();
	}

	void DX12PSOCache::Clear() noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_GraphicsPSOMap.clear();
		m_ComputePSOMap.clear();
	}

	void DX12PSOCache::ClearGraphicsPSOCache() noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_GraphicsPSOMap.clear();
	}

	void DX12PSOCache::ClearComputePSOCache() noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_ComputePSOMap.clear();
	}
}