#pragma once

#include "Graphics/DX12/DX12Device.h"
#include "Graphics/DX12/DX12Fence.h"
#include "Graphics/DX12/DX12RingBuffer.h"

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>
#include <d3dx12.h>

namespace gglab
{
	class DX12RingConstantBuffer
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			uint32_t m_CapacityInBytes = 256 * 1024;
		};

	public:
		explicit DX12RingConstantBuffer(const CreateInfo& createInfo) noexcept
		{
			GGLAB_ASSERT_NOT_NULL(createInfo.m_DX12Device);
			GGLAB_ASSERT_MSG(createInfo.m_CapacityInBytes > 0,
				"DX12RingConstantBuffer capacity must be greater than zero.");

			auto resourceCreateInfo = DX12Buffer::UploadBufferCreateInfo(
				createInfo.m_DX12Device->GetMemAllocator(),
				createInfo.m_CapacityInBytes);
			m_RingBuffer.Create(resourceCreateInfo, createInfo.m_CapacityInBytes);
			m_RingBuffer.GetBuffer()->SetDebugName(L"Renderer.PassConstantBufferRing");
		}

		GGLAB_DELETE_COPYABLE_MOVABLE(DX12RingConstantBuffer);
		~DX12RingConstantBuffer() = default;

		template<typename T>
		D3D12_GPU_VIRTUAL_ADDRESS Upload(const T& data) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);
			static_assert(std::is_standard_layout_v<T>);
			static_assert(sizeof(T) > 0);

			constexpr uint32_t ConstantBufferAlignment =
				D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
			auto span = m_RingBuffer.Upload(
				&data,
				static_cast<uint32_t>(sizeof(T)),
				ConstantBufferAlignment);
			GGLAB_ASSERT_MSG(span.IsValid(),
				"DX12RingConstantBuffer failed to allocate constant buffer space.");
			if (!span.IsValid())
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12RingConstantBuffer::Upload failed: not enough free space for {} bytes.",
					sizeof(T));
				return 0;
			}

			m_PendingSpans.push_back(span);
			return m_RingBuffer.GetGPUVirtualAddressAtOffset(span.m_Offset);
		}

		void RetirePending(const DX12FencePoint& fencePoint) noexcept
		{
			GGLAB_ASSERT_MSG(fencePoint.IsValid(),
				"DX12RingConstantBuffer requires a valid graphics fence point.");

			for (const auto& span : m_PendingSpans)
			{
				m_RingBuffer.Retire(span, fencePoint);
			}
			m_PendingSpans.clear();
		}

		void ReclaimCompleted(const DX12FencePoint& graphicsFencePoint) noexcept
		{
			if (!graphicsFencePoint.IsValid())
			{
				return;
			}

			const auto* fence = graphicsFencePoint.GetFence();
			GGLAB_ASSERT_NOT_NULL(fence);
			m_RingBuffer.ReclaimCompleted(fence->GetCompletedValue());
		}

		uint32_t GetCapacity() const noexcept { return m_RingBuffer.GetCapacity(); }
		uint32_t GetHighWater() const noexcept { return m_RingBuffer.GetHighWater(); }

	private:
		DX12RingBuffer m_RingBuffer;
		std::vector<DX12RingBuffer::Span> m_PendingSpans;
	};
}
