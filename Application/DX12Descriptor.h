#pragma once
#include "DX12FencePoint.h"

namespace gglab
{
	class DX12Device;
	class DX12DescriptorAllocatorBase;

	struct DX12DescriptorView
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE m_CpuHandle = {};
		CD3DX12_GPU_DESCRIPTOR_HANDLE m_GpuHandle = {};
	};

	struct DX12DescriptorID
	{
		static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();
		uint32_t m_Index = InvalidIndex;
		uint32_t m_Generation = 0;

		bool IsValid() const noexcept { return m_Index != InvalidIndex; }
	};

	class DX12DescriptorHandle
	{
	public:
		using IndexType = uint32_t;

	public:
		DX12DescriptorHandle() noexcept = default;
		GGLAB_DELETE_COPYABLE(DX12DescriptorHandle);
		DX12DescriptorHandle(DX12DescriptorHandle&& rhs) noexcept;
		DX12DescriptorHandle& operator=(DX12DescriptorHandle&& rhs) noexcept;
		~DX12DescriptorHandle();

		bool IsValid() const noexcept;
		bool IsShaderVisible() const noexcept;

		D3D12_DESCRIPTOR_HEAP_TYPE Type() const noexcept { return m_Type; }
		uint32_t Index() const noexcept { return m_Index; }
		uint32_t Count() const noexcept { return m_Count; }
		uint32_t IncrementSize() const noexcept { return m_IncrementSize; }

		CD3DX12_CPU_DESCRIPTOR_HANDLE CpuHandle() const noexcept { return m_CpuHandle; }
		CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandle() const noexcept { return m_GpuHandle; }
		CD3DX12_CPU_DESCRIPTOR_HANDLE CpuHandleAt(IndexType index) const noexcept;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandleAt(IndexType index) const noexcept;

		DX12DescriptorAllocatorBase* Owner() const noexcept { return m_Owner; }

		explicit operator bool() const noexcept { return IsValid(); }

		void Free() noexcept;
		void Retire(const DX12FencePoint& fencePoint) noexcept;
		void Reset() noexcept;

		DX12DescriptorView ToView(IndexType index = 0) const noexcept;

	private:
		void MoveFrom(DX12DescriptorHandle& rhs) noexcept;

	private:
		static constexpr IndexType InvalidIndex = std::numeric_limits<IndexType>::max();

	private:
		D3D12_DESCRIPTOR_HEAP_TYPE m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		IndexType m_Index = InvalidIndex;
		uint32_t m_Count = 0;
		uint32_t m_IncrementSize = 0;
		CD3DX12_CPU_DESCRIPTOR_HANDLE m_CpuHandle = {};
		CD3DX12_GPU_DESCRIPTOR_HANDLE m_GpuHandle = {};

		DX12DescriptorAllocatorBase* m_Owner = nullptr;

	private:
		friend class DX12DescriptorAllocatorBase;
	};
}