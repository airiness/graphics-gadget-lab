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
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorManager);
		~DX12DescriptorManager();

		void Tick() noexcept;
		void EndFrame(const DX12FencePoint& fencePoint) noexcept;

		DX12DescriptorFreeListAllocator& GetCbvSrvUavDescriptorAllocator() noexcept { return *m_CbvSrvUavDescriptorAllocator; }
		const DX12DescriptorFreeListAllocator& GetCbvSrvUavDescriptorAllocator() const noexcept { return *m_CbvSrvUavDescriptorAllocator; }
		
		DX12DescriptorFreeListAllocator& GetRtvDescriptorAllocator() noexcept { return *m_RtvDescriptorAllocator; };
		const DX12DescriptorFreeListAllocator& GetRtvDescriptorAllocator() const noexcept { return *m_RtvDescriptorAllocator; };
		
		DX12DescriptorFreeListAllocator& GetDsvDescriptorAllocator() noexcept { return *m_DsvDescriptorAllocator; };
		const DX12DescriptorFreeListAllocator& GetDsvDescriptorAllocator() const noexcept { return *m_DsvDescriptorAllocator; };
		
		DX12DescriptorFreeListAllocator& GetSamplerDescriptorAllocator() noexcept { return *m_SamplerDescriptorAllocator; };
		const DX12DescriptorFreeListAllocator& GetSamplerDescriptorAllocator() const noexcept { return *m_SamplerDescriptorAllocator; };

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