#pragma once
#include "Graphics/DX12/DX12FencePoint.h"

namespace gglab
{
	class DX12Device;
	class DX12DescriptorAllocatorBase;

	struct DX12DescriptorRange
	{
		uint32_t m_Begin = 0;
		uint32_t m_Count = 0;

		bool IsValid() const noexcept { return m_Count > 0; }
		uint32_t End() const noexcept { return m_Begin + m_Count; }
	};

	struct DX12DescriptorSpan
	{
		static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();
		uint32_t m_Index = InvalidIndex;	// Heap global index
		uint32_t m_Count = 0;

		bool IsValid() const noexcept { return m_Index != InvalidIndex && m_Count > 0; }
		uint32_t End() const noexcept { return m_Index + m_Count; }
	};

	struct DX12DescriptorView
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE m_CpuHandle = {};
		CD3DX12_GPU_DESCRIPTOR_HANDLE m_GpuHandle = {};

#if defined(BUILD_DEBUG)
		D3D12_DESCRIPTOR_HEAP_TYPE m_DebugType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
#endif

		bool IsValid() const noexcept { return m_CpuHandle.ptr != 0; }
		explicit operator bool() const noexcept { return IsValid(); }
	};

	struct DX12DescriptorID
	{
		static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();
		uint32_t m_Index = InvalidIndex;	//  Heap global index
		uint32_t m_Generation = 0;

		bool IsValid() const noexcept { return m_Index != InvalidIndex; }
	};

	class DX12DescriptorHandle
	{
	public:
		DX12DescriptorHandle() noexcept = default;
		GGLAB_DELETE_COPYABLE(DX12DescriptorHandle);
		DX12DescriptorHandle(DX12DescriptorHandle&& rhs) noexcept;
		DX12DescriptorHandle& operator=(DX12DescriptorHandle&& rhs) noexcept;
		~DX12DescriptorHandle();

		bool IsValid() const noexcept;
		explicit operator bool() const noexcept { return IsValid(); }

		bool IsShaderVisible() const noexcept;

		uint32_t Index() const noexcept { return m_Index; }
		uint32_t Count() const noexcept { return m_Count; }
		uint32_t Generation() const noexcept { return m_Generation; }

		CD3DX12_CPU_DESCRIPTOR_HANDLE CpuHandleAt(uint32_t offset = 0) const noexcept;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandleAt(uint32_t offset = 0) const noexcept;

		D3D12_DESCRIPTOR_HEAP_TYPE HeapType() const noexcept;

		DX12DescriptorAllocatorBase* OwnerAllocator() const noexcept { return m_OwnerAllocator; }

		DX12DescriptorView ToDescriptorView(uint32_t offset = 0) const noexcept;
		DX12DescriptorID ToDescriptorId() const noexcept;

		void Free() noexcept;
		void Retire(const DX12FencePoint& fencePoint) noexcept;

	private:
		void Reset() noexcept;
		void MoveFrom(DX12DescriptorHandle& rhs) noexcept;

	private:
		static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

	private:
		uint32_t m_Index = InvalidIndex;
		uint32_t m_Count = 0;
		uint32_t m_Generation = 0;
		DX12DescriptorAllocatorBase* m_OwnerAllocator = nullptr;

		friend class DX12DescriptorAllocatorBase;
	};
}