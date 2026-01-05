#pragma once
#include "DX12FencePoint.h"

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
		uint32_t m_Index = InvalidIndex;
		uint32_t m_Count = 0;

		bool IsValid() const noexcept { return m_Index != InvalidIndex && m_Count > 0; }
		uint32_t End() const noexcept { return m_Index + m_Count; }
	};

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
		DX12DescriptorHandle() noexcept = default;
		GGLAB_DELETE_COPYABLE(DX12DescriptorHandle);
		DX12DescriptorHandle(DX12DescriptorHandle&& rhs) noexcept;
		DX12DescriptorHandle& operator=(DX12DescriptorHandle&& rhs) noexcept;
		~DX12DescriptorHandle();

		bool IsValid() const noexcept;
		bool IsShaderVisible() const noexcept;

		uint32_t Index() const noexcept { return m_Index; }
		uint32_t Count() const noexcept { return m_Count; }

		DX12DescriptorView ToView(uint32_t localIndex = 0) const noexcept;
		CD3DX12_CPU_DESCRIPTOR_HANDLE CpuHandle(uint32_t localIndex = 0) const noexcept;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandle(uint32_t localIndex = 0) const noexcept;

		DX12DescriptorAllocatorBase* Owner() const noexcept { return m_Owner; }

		explicit operator bool() const noexcept { return IsValid(); }

		void Free() noexcept;
		void Retire(const DX12FencePoint& fencePoint) noexcept;
		void Reset() noexcept;

	private:
		void MoveFrom(DX12DescriptorHandle& rhs) noexcept;

	private:
		static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

	private:
		uint32_t m_Index = InvalidIndex;
		uint32_t m_Count = 0;

		DX12DescriptorAllocatorBase* m_Owner = nullptr;

	private:
		friend class DX12DescriptorAllocatorBase;
	};
}