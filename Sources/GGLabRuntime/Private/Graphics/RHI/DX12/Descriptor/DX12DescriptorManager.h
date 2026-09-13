#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/TypeUtils.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIDescriptorCapacityContract.h"

#include <array>
#include <cstdint>
#include <memory>

namespace gglab
{
	class DX12Device;
	class DX12FencePoint;
	class DX12DescriptorHeap;
	class DX12DescriptorFreeListAllocator;

	class DX12DescriptorManager
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;

			uint32_t m_CbvSrvUavCount =
				GGLabDescriptorCapacityContract.m_ResourceDescriptorCount;
			uint32_t m_RtvCount = 4096;
			uint32_t m_DsvCount = 1024;
			uint32_t m_SamplerCount = GGLabDescriptorCapacityContract.m_SamplerDescriptorCount;

			uint32_t m_DevelopGuiSrvCount = 1024;
		};

		enum class HeapType : uint8_t
		{
			CbvSrvUav,
			Rtv,
			Dsv,
			Sampler,

			Count
		};

		enum class AllocatorType : uint8_t
		{
			GeneralCbvSrvUav,
			GeneralRtv,
			GeneralDsv,
			GeneralSampler,
			DevelopGuiSrv,

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
		DX12DescriptorFreeListAllocator* GetFreeListAllocator(
			AllocatorType allocatorType) const noexcept;

		DX12DescriptorView AllocateDevelopGuiSrvView() noexcept;
		void DeferFreeDevelopGuiSrvInFrame(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) noexcept;
		void DeferFreeDevelopGuiSrvInFrame(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) noexcept;

	private:
		using HeapArray =
			std::array<std::unique_ptr<DX12DescriptorHeap>, utils::EnumCount<HeapType>()>;
		using FreeListAllocatorArray = std::array<std::unique_ptr<DX12DescriptorFreeListAllocator>,
			utils::EnumCount<AllocatorType>()>;

		DX12Device* m_DX12Device = nullptr;
		HeapArray m_Heaps;
		FreeListAllocatorArray m_FreeListAllocators;
	};
}
