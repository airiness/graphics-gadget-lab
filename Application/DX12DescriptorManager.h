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

		enum class FreeListAllocatorType : uint8_t
		{
			GeneralCbvSrvUav,
			GeneralRtv,
			GeneralDsv,
			DevelopGui,
			BindlessTexture,

			Count
		};

	public:
		explicit DX12DescriptorManager(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorManager);
		~DX12DescriptorManager();

		void Tick() noexcept;
		void EndFrame(const DX12FencePoint& fencePoint) noexcept;

		DX12DescriptorFreeListAllocator& GetFreeListAllocator(FreeListAllocatorType type) noexcept;

	private:
		using FreeListAllocatorArray =
			std::array<std::unique_ptr<DX12DescriptorFreeListAllocator>, static_cast<uint8_t>(FreeListAllocatorType::Count)>;

		std::unique_ptr<DX12DescriptorHeap> m_CbvSrvUavHeap;
		std::unique_ptr<DX12DescriptorHeap> m_RtvHeap;
		std::unique_ptr<DX12DescriptorHeap> m_DsvHeap;
		std::unique_ptr<DX12DescriptorHeap> m_SamplerHeap;

		FreeListAllocatorArray m_FreeListAllocators;

	};
}