#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Cache/DX12PSOCache.h"
#include "Graphics/RHI/DX12/DX12PipelineState.h"
#include "Graphics/RHI/DX12/Utility/DX12InputLayoutUtils.h"

namespace gglab
{
	DX12PSOCache::DX12PSOCache(DX12Device* dx12Device, std::unique_ptr<IPSOCreator> creator) noexcept :
		m_DX12Device(dx12Device),
		m_Creator(std::move(creator))
	{
	}

	DX12PSOCache::~DX12PSOCache() = default;

	DX12PipelineState* DX12PSOCache::GetOrCreate(const GraphicsPSOKey& key, const GraphicsPipelineDesc& desc) noexcept
	{
		{
			std::shared_lock lock(m_Mutex);
			auto iter = m_GraphicsPSOMap.find(key);
			if (iter != m_GraphicsPSOMap.end())
			{
				return iter->second.get();
			}
		}

		std::unique_lock lock(m_Mutex);
		auto iter = m_GraphicsPSOMap.find(key);
		if (iter != m_GraphicsPSOMap.end())
		{
			return iter->second.get();
		}

		GGLAB_ASSERT_MSG(m_Creator, "PSO Creator is null");
		auto pso = m_Creator->CreateGraphicsPSO(m_DX12Device, desc);
		GGLAB_ASSERT_MSG(pso, "Failed to create PSO");

		auto& slot = m_GraphicsPSOMap[key];
		slot = std::move(pso);
		return slot.get();
	}

	DX12PipelineState* DX12PSOCache::GetOrCreate(
		const RHIGraphicsPipelineDesc& desc,
		const RootSignatureHandle& rootSignature,
		const DX12GraphicsPipelineShaderInputs& shaderInputs) noexcept
	{
		const DX12RHIGraphicsPSOKey key = MakeDX12RHIGraphicsPSOKey(
			desc,
			rootSignature.m_Id,
			shaderInputs);

		{
			std::shared_lock lock(m_Mutex);
			auto iter = m_RHIGraphicsPSOMap.find(key);
			if (iter != m_RHIGraphicsPSOMap.end())
			{
				return iter->second.get();
			}
		}

		std::unique_lock lock(m_Mutex);
		auto iter = m_RHIGraphicsPSOMap.find(key);
		if (iter != m_RHIGraphicsPSOMap.end())
		{
			return iter->second.get();
		}

		DX12InputLayoutBuildResult inputLayout{};
		BuildDX12InputLayoutDesc(desc.m_VertexInput, inputLayout);

		GraphicsPipelineDesc dx12Desc = BuildDX12GraphicsPipelineDesc(
			desc,
			rootSignature,
			shaderInputs,
			inputLayout);

		GGLAB_ASSERT_MSG(m_Creator, "PSO Creator is null");
		auto pso = m_Creator->CreateGraphicsPSO(m_DX12Device, dx12Desc);
		GGLAB_ASSERT_MSG(pso, "Failed to create PSO");

		auto& slot = m_RHIGraphicsPSOMap[key];
		slot = std::move(pso);
		return slot.get();
	}

	DX12PipelineState* DX12PSOCache::GetOrCreate(const ComputePSOKey& key, const ComputePipelineDesc& desc) noexcept
	{
		{
			std::shared_lock lock(m_Mutex);
			auto iter = m_ComputePSOMap.find(key);
			if (iter != m_ComputePSOMap.end())
			{
				return iter->second.get();
			}
		}

		std::unique_lock lock(m_Mutex);
		auto iter = m_ComputePSOMap.find(key);
		if (iter != m_ComputePSOMap.end())
		{
			return iter->second.get();
		}
		GGLAB_ASSERT_MSG(m_Creator, "PSO Creator is null");
		auto pso = m_Creator->CreateComputePSO(m_DX12Device, desc);
		GGLAB_ASSERT_MSG(pso, "Failed to create PSO");
		auto& slot = m_ComputePSOMap[key];
		slot = std::move(pso);
		return slot.get();
	}

	void DX12PSOCache::Clear() noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_GraphicsPSOMap.clear();
		m_RHIGraphicsPSOMap.clear();
		m_ComputePSOMap.clear();
		m_Revision.fetch_add(1, std::memory_order_relaxed);
	}

	void DX12PSOCache::ClearGraphicsPSOCache() noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_GraphicsPSOMap.clear();
		m_RHIGraphicsPSOMap.clear();
		m_Revision.fetch_add(1, std::memory_order_relaxed);
	}

	void DX12PSOCache::ClearComputePSOCache() noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_ComputePSOMap.clear();
		m_Revision.fetch_add(1, std::memory_order_relaxed);
	}
}
