#pragma once
#include "DX12Device.h"
#include "DX12Buffer.h"
#include "DX12Descriptor.h"
#include "DX12RingBuffer.h"
#include "MathUtils.h"

namespace gglab
{
	template<typename T>
	class DX12RingStructuredBuffer
	{
	public:
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
		explicit DX12RingStructuredBuffer(DX12Device* dx12Device, uint32_t elementsCapacity) noexcept :
			m_ElementCapacity(elementsCapacity)
		{
			GGLAB_ASSERT_MSG(dx12Device != nullptr, "DX12Device pointer can not be null.");
			GGLAB_ASSERT_MSG(elementsCapacity > 0, "DX12RingStructuredBuffer elementsCapacity must be greater than zero.");

			const uint32_t stride = GetElementStride();

			GGLAB_ASSERT_MSG(static_cast<uint64_t>(stride) * elementsCapacity <= UINT32_MAX,
				"DX12RingStructuredBuffer: total bytes overflow.");

			const auto totalSizeInBytes = stride * elementsCapacity;

			auto createInfo = DX12Buffer::VertexOrIndexBufferCreateInfo(dx12Device->GetMemAllocator(), totalSizeInBytes);

			m_RingBuffer = std::make_unique<DX12RingBuffer>(createInfo, totalSizeInBytes);

			m_SrvDescriptor = dx12Device->GetCbvSrvUavDescriptorAllocator()->Allocate();
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = elementsCapacity;
			srvDesc.Buffer.StructureByteStride = stride;
			srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			dx12Device->Get()->CreateShaderResourceView(m_RingBuffer->GetResource(), &srvDesc, m_SrvDescriptor.CpuHandle());

		}
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12RingStructuredBuffer);
		~DX12RingStructuredBuffer() = default;

		AllocateResult Allocate(uint32_t elementCount, uint32_t alignment = GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE) noexcept
		{
			AllocateResult result{};
			if (elementCount == 0)
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12RingStructuredBuffer::Allocate called with elementCount = 0.");
				return result;
			}

			const uint32_t stride = GetElementStride();
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

		DX12DescriptorView GetSRVDescriptorView() const noexcept
		{
			return m_SrvDescriptor.ToView();
		}

		DX12Buffer* GetBuffer() const noexcept { return m_RingBuffer->GetBuffer(); }
		uint32_t GetElementCapacity() const noexcept { return m_ElementCapacity; }
		uint32_t GetElementStride() const noexcept { return static_cast<uint32_t>(sizeof(T)); }

	private:
		std::unique_ptr<DX12RingBuffer> m_RingBuffer;
		DX12Descriptor m_SrvDescriptor{};
		uint32_t m_ElementCapacity = 0;
	};
}