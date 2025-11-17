#pragma once
#include "DX12Device.h"
#include "DX12Buffer.h"
#include "MathUtils.h"

namespace gglab
{
	class DX12Buffer;

	template<typename T>
	class DX12ConstantBuffer
	{
	public:
		explicit DX12ConstantBuffer(DX12Device* dx12Device, uint32_t framesInFlight = 1) noexcept :
			m_Frames(framesInFlight ? framesInFlight : 1),
			m_StrideAligned(utils::AlignUpPow2(static_cast<uint32_t>(sizeof(T)), 
				static_cast<uint32_t>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))),
			m_TotalSize(m_StrideAligned* m_Frames)
		{
			m_Buffer = std::make_unique<DX12Buffer>();
			DX12Resource::CreateInfo createInfo{};
			createInfo.m_Allocator = dx12Device->GetMemAllocator();
			createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
			createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
			createInfo.m_InitStates = D3D12_RESOURCE_STATE_GENERIC_READ;
			createInfo.m_ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(m_TotalSize);
			m_Buffer->Create(createInfo);

			// Data mapping
			m_MappedData = static_cast<std::byte*>(m_Buffer->Map());
			if (m_MappedData)
			{
				std::memset(m_MappedData, 0, m_TotalSize);
			}

			// Get base GPU address 
			m_BaseGpuVirtualAddress = m_Buffer->GPUVirtualAddress();

		}
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12ConstantBuffer);

		~DX12ConstantBuffer()
		{
			if (m_Buffer && m_MappedData)
			{
				m_Buffer->Unmap();
				m_MappedData = nullptr;
			}
		}

		void Update(const T& data, uint32_t frameIndex = 0) noexcept
		{
			GGLAB_ASSERT_MSG(m_MappedData, "ConstantBuffer mapped pointer is nullptr.");
			const auto sliceOffset = (frameIndex % m_Frames) * m_StrideAligned;
			std::memcpy(m_MappedData + sliceOffset, &data, sizeof(T));
		}

		D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress(uint32_t frameIndex = 0) const noexcept
		{
			return m_BaseGpuVirtualAddress + static_cast<uint64_t>((frameIndex % m_Frames) * m_StrideAligned);
		}

		D3D12_GPU_VIRTUAL_ADDRESS UpdateAndGetGPUVirtualAddress(uint32_t frameIndex, const T& data) noexcept
		{
			Update(data, frameIndex);
			return GetGPUVirtualAddress(frameIndex);
		}

		D3D12_CONSTANT_BUFFER_VIEW_DESC MakeCBVDesc(uint32_t frameIndex = 0) const noexcept
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
			desc.BufferLocation = GetGPUVirtualAddress(frameIndex);
			desc.SizeInBytes = m_StrideAligned;
			return desc;
		}

		uint32_t GetStrideAligned() const noexcept { return m_StrideAligned; }
		uint32_t GetTotalSize() const noexcept { return m_TotalSize; }
		uint32_t GetFrameCount() const noexcept { return m_Frames; }
		DX12Buffer* GetBuffer() const noexcept { return m_Buffer.get(); }

	private:
		std::unique_ptr<DX12Buffer> m_Buffer;
		std::byte* m_MappedData = nullptr;

		uint32_t m_Frames = 1;
		uint32_t m_StrideAligned = 0;
		uint32_t m_TotalSize = 0;

		D3D12_GPU_VIRTUAL_ADDRESS m_BaseGpuVirtualAddress = 0;
	};
}