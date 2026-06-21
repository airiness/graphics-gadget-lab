#pragma once
#include "Graphics/DX12/DX12Device.h"
#include "Graphics/DX12/DX12Buffer.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorTypes.h"
#include "Graphics/DX12/DX12RingBuffer.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorFreeListAllocator.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorManager.h"
#include "Core/Utility/MathUtils.h"

namespace gglab
{
	template<typename T>
	class DX12RingStructuredBuffer
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			uint32_t m_ElementsCapacity = 4096;
		};

		struct AllocateResult
		{
			uint32_t m_FirstElementIndex = 0;
			uint32_t m_ElementCount = 0;
			uint32_t m_OffsetInBytes = 0;
			uint32_t m_SizeInBytes = 0;
			DX12RingBuffer::Span m_TargetSpan{};

			bool IsValid() const noexcept { return m_ElementCount > 0; }
		};

	public:
		explicit DX12RingStructuredBuffer(const CreateInfo& createInfo) noexcept :
			m_ElementCapacity(createInfo.m_ElementsCapacity)
		{
			GGLAB_ASSERT_MSG(createInfo.m_DX12Device != nullptr, "DX12Device pointer can not be null.");
			GGLAB_ASSERT_MSG(m_ElementCapacity > 0, "DX12RingStructuredBuffer elementsCapacity must be greater than zero.");

			const uint32_t stride = GetElementStride();
			const uint32_t totalSizeInBytes = stride * m_ElementCapacity;

			GGLAB_ASSERT_MSG(static_cast<uint64_t>(stride) * m_ElementCapacity <= UINT32_MAX,
				"DX12RingStructuredBuffer: total bytes overflow.");
			
			DX12Resource::CreateInfo resourceCreateInfo{};
			resourceCreateInfo.m_Allocator = createInfo.m_DX12Device->GetMemAllocator();
			resourceCreateInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
			resourceCreateInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
			resourceCreateInfo.m_InitStates = D3D12_RESOURCE_STATE_COMMON;
			resourceCreateInfo.m_ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(totalSizeInBytes));

			m_RingBuffer = std::make_unique<DX12RingBuffer>();
			m_RingBuffer->Create(resourceCreateInfo, totalSizeInBytes);
		}
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12RingStructuredBuffer);
		~DX12RingStructuredBuffer() = default;

		AllocateResult Allocate(uint32_t elementCount) noexcept
		{
			AllocateResult result{};
			if (elementCount == 0)
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12RingStructuredBuffer::Allocate called with elementCount = 0.");
				return result;
			}

			const uint32_t stride = GetElementStride();
			const uint32_t alignment = stride;
			const uint32_t sizeInBytes = stride * elementCount;

			auto span = m_RingBuffer->Allocate(sizeInBytes, alignment);
			if (!span.IsValid())
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12RingStructuredBuffer::Allocate failed: not enough free space for element count {}.", elementCount);
				return result;
			}

			result.m_FirstElementIndex = span.m_Offset / stride;
			result.m_ElementCount = elementCount;
			result.m_OffsetInBytes = span.m_Offset;
			result.m_SizeInBytes = sizeInBytes;
			result.m_TargetSpan = span;
			return result;
		}

		void Retire(const AllocateResult& allocation, const DX12FencePoint& fencePoint) noexcept
		{
			if (!allocation.IsValid())
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12RingStructuredBuffer::Retire called with invalid allocation.");
				return;
			}

			m_RingBuffer->Retire(allocation.m_TargetSpan, fencePoint);
		}

		void ReclaimCompleted(const DX12FencePoint& fencePoint) noexcept
		{
			m_RingBuffer->ReclaimCompleted(fencePoint);
		}

		void ReclaimCompleted(uint64_t completedFenceValue) noexcept
		{
			m_RingBuffer->ReclaimCompleted(completedFenceValue);
		}

		DX12Buffer* GetBuffer() const noexcept { return m_RingBuffer->GetBuffer(); }
		uint32_t GetElementCapacity() const noexcept { return m_ElementCapacity; }
		uint32_t GetElementStride() const noexcept { return static_cast<uint32_t>(sizeof(T)); }

	private:
		std::unique_ptr<DX12RingBuffer> m_RingBuffer;
		uint32_t m_ElementCapacity = 0;
	};
}
