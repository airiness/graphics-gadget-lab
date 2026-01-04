#pragma once
#include "DX12FencePoint.h"

namespace gglab
{
	class DX12Device;
	class DX12DescriptorHeap;
	class DX12DescriptorFreeListAllocator;
	class DX12DescriptorManager
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;

			uint32_t m_CbvSrvUavCount = 65536;
			uint32_t m_RtvCount = 4096;
			uint32_t m_DsvCount = 1024;
			uint32_t m_SamplerCount = 2048;
		};

	public:
		explicit DX12DescriptorManager(const CreateInfo& createInfo) noexcept;
		~DX12DescriptorManager() = default;

		void Tick() noexcept;
		void EndFrame(const DX12FencePoint& fencePoint) noexcept;

		DX12DescriptorFreeListAllocator* GetCbvSrvUavDescriptorAllocator() noexcept { return m_CbvSrvUavDescriptorAllocator.get(); }
		DX12DescriptorFreeListAllocator* GetRtvDescriptorAllocator() noexcept { return m_RtvDescriptorAllocator.get(); };
		DX12DescriptorFreeListAllocator* GetDsvDescriptorAllocator() noexcept { return m_DsvDescriptorAllocator.get(); };
		DX12DescriptorFreeListAllocator* GetSamplerDescriptorAllocator() noexcept { return m_SamplerDescriptorAllocator.get(); };

	private:
		std::unique_ptr<DX12DescriptorHeap> m_CbvSrvUavHeap;
		std::unique_ptr<DX12DescriptorHeap> m_RtvHeap;
		std::unique_ptr<DX12DescriptorHeap> m_DsvHeap;
		std::unique_ptr<DX12DescriptorHeap> m_SamplerHeap;

		std::unique_ptr<DX12DescriptorFreeListAllocator> m_CbvSrvUavDescriptorAllocator;
		std::unique_ptr<DX12DescriptorFreeListAllocator> m_RtvDescriptorAllocator;
		std::unique_ptr<DX12DescriptorFreeListAllocator> m_DsvDescriptorAllocator;
		std::unique_ptr<DX12DescriptorFreeListAllocator> m_SamplerDescriptorAllocator;
	};
}