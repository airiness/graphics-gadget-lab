#pragma once
#include "DX12FencePoint.h"
#include "DX12DescriptorTypes.h"
#include "TypeUtils.h"

namespace gglab
{
	class DX12Device;
	class DX12DescriptorHeap;
	class DX12DescriptorFreeListAllocator;
	class DX12Texture;
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

		enum class AllocatorType : uint8_t
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

		struct TextureSrvCreateInfo
		{
			DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;
			D3D12_SRV_DIMENSION m_Dimension = D3D12_SRV_DIMENSION_TEXTURE2D;

			uint32_t m_MostDetailedMip = 0;
			uint32_t m_MipLevels = std::numeric_limits<uint32_t>::max();
			uint32_t m_FirstArraySlice = 0;
			uint32_t m_ArraySize = std::numeric_limits<uint32_t>::max();
			uint32_t m_PlaneSlice = 0;
			float m_ResourceMinLODClamp = 0.0f;
		};

	public:
		explicit DX12DescriptorManager(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorManager);
		~DX12DescriptorManager();
		
		void Tick() noexcept;
		void EndFrame(const DX12FencePoint& fencePoint) noexcept;

		DX12DescriptorHeap* GetHeap(HeapType heapType) const noexcept;
		DX12DescriptorFreeListAllocator* GetFreeListAllocator(AllocatorType allocatorType) const noexcept;

		DX12DescriptorID AllocateBindlessSrvId() noexcept;

		DX12DescriptorID CreateBindlessSrv(DX12Texture* texture, const TextureSrvCreateInfo& info = {}) noexcept;
		void WriteBindlessSrv(const DX12DescriptorID& descriptorId,
			DX12Texture* texture, const TextureSrvCreateInfo& info = {}) noexcept;
		uint32_t BindlessSrvIdToGlobalIndex(const DX12DescriptorID& descriptorId) const noexcept;

		void RetireBindlessSrvId(const DX12DescriptorID& descriptorId, const DX12FencePoint& fencePoint) noexcept;
		DX12DescriptorView BindlessSrvIdToView(const DX12DescriptorID& descriptorId) const noexcept;

		DX12DescriptorView AllocateDevelopGuiSrvView() noexcept;

		void DeferFreeDevelopGuiSrvInFrame(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) noexcept;
		void DeferFreeDevelopGuiSrvInFrame(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) noexcept;

	private:
		using HeapArray = std::array<std::unique_ptr<DX12DescriptorHeap>, utils::EnumCount<HeapType>()>;
		using FreeListAllocatorArray =
			std::array<std::unique_ptr<DX12DescriptorFreeListAllocator>, utils::EnumCount<AllocatorType>()>;

		DX12Device* m_DX12Device = nullptr;
		HeapArray m_Heaps;
		FreeListAllocatorArray m_FreeListAllocators;
	};
}