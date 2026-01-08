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

			uint32_t m_DevelopGuiSrvCount = 1024;
			uint32_t m_BindlessSrvCount = 26000;
		};

		enum class HeapType : uint8_t
		{
			CbvSrvUav,
			Rtv,
			Dsv,
			Sampler,

			Count
		};

		enum class FreeListAllocatorType : uint8_t
		{
			GeneralCbvSrvUav,
			GeneralRtv,
			GeneralDsv,
			GeneralSampler,
			DevelopGuiSrv,
			BindlessSrv,

			Count,
			Invalid = Count
		};

	public:
		explicit DX12DescriptorManager(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorManager);
		~DX12DescriptorManager();
		
		void Tick() noexcept;
		void EndFrame(const DX12FencePoint& fencePoint) noexcept;

		DX12DescriptorHeap* GetHeap(HeapType heapType) const noexcept;
		DX12DescriptorFreeListAllocator* GetFreeListAllocator(FreeListAllocatorType allocatorType) const noexcept;

	private:
		using HeapArray = std::array<std::unique_ptr<DX12DescriptorHeap>, static_cast<uint8_t>(HeapType::Count)>;
		using FreeListAllocatorArray =
			std::array<std::unique_ptr<DX12DescriptorFreeListAllocator>, static_cast<uint8_t>(FreeListAllocatorType::Count)>;

		HeapArray m_Heaps;
		FreeListAllocatorArray m_FreeListAllocators;
	};
}