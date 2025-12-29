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

	class DX12Descriptor
	{
	public:
		using IndexType = uint32_t;

	public:
		DX12Descriptor() noexcept = default;
		GGLAB_DELETE_COPYABLE(DX12Descriptor);
		DX12Descriptor(DX12Descriptor&& rhs) noexcept;
		DX12Descriptor& operator=(DX12Descriptor&& rhs) noexcept;
		~DX12Descriptor();

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
		void MoveFrom(DX12Descriptor& rhs) noexcept;

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

	class DX12DescriptorHeap
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			D3D12_DESCRIPTOR_HEAP_TYPE m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			D3D12_DESCRIPTOR_HEAP_FLAGS m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			uint32_t m_DescriptorCount = 0;
		};

	public:
		explicit DX12DescriptorHeap(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12DescriptorHeap);
		~DX12DescriptorHeap() = default;

		uint32_t DescriptorCount() const noexcept { return m_DescriptorCount; }
		uint32_t DescriptorIncrementSize() const noexcept { return m_IncrementSize; }

		ID3D12DescriptorHeap* Get() const noexcept { return m_D3D12DescriptorHeap.Get(); }
		D3D12_DESCRIPTOR_HEAP_TYPE Type() const noexcept { return m_Type; }
		D3D12_DESCRIPTOR_HEAP_FLAGS Flags() const noexcept { return m_Flags; }

		CD3DX12_CPU_DESCRIPTOR_HANDLE CpuStart() const noexcept;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GpuStart() const noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		D3D12_DESCRIPTOR_HEAP_TYPE m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		D3D12_DESCRIPTOR_HEAP_FLAGS m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		uint32_t m_DescriptorCount = 0;
		uint32_t m_IncrementSize = 0;
		ComPtr<ID3D12DescriptorHeap> m_D3D12DescriptorHeap;
	};
}