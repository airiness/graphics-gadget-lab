#pragma once
#include "DX12Descriptor.h"
#include "DX12FencePoint.h"
#include "AllocatorBase.h"

namespace gglab
{
	class DX12Device;
	class DX12DescriptorAllocatorBase
	{
	public:
		using IndexType = uint32_t;
		using CountType = uint32_t;

		struct CreateInfo
		{
			DX12DescriptorHeap* m_DescriptorHeap = nullptr;
		};

	public:
		explicit DX12DescriptorAllocatorBase(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12DescriptorAllocatorBase);
		virtual ~DX12DescriptorAllocatorBase() = default;

		DX12DescriptorHeap* GetHeap() const noexcept { return m_DescriptorHeap; }

	protected:
		DX12DescriptorHandle CreateDescriptor(const AllocatorBase::IndexSpan& indexSpan) noexcept;

		virtual void FreeDescriptorInternal(DX12DescriptorHandle&) noexcept;
		virtual void RetireDescriptorInternal(const DX12DescriptorHandle&, const DX12FencePoint&) noexcept;

	protected:
		//std::unique_ptr<DX12DescriptorHeap> m_Heap;

		DX12DescriptorHeap* m_DescriptorHeap;

		friend class DX12DescriptorHandle;
	};
}