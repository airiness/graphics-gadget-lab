#pragma once
#include "DX12Device.h"
#include "DX12Buffer.h"

namespace graphicsGadgetLab
{
	class DX12Buffer;

	template<typename CBType>
	class DX12ConstantBuffer
	{
	public:
		explicit DX12ConstantBuffer(DX12Device* device) noexcept;
		~DX12ConstantBuffer();

		void Update(const CBType& data) noexcept;
		uint32_t GetBufferSize() const noexcept { return m_BufferSize; }
		DX12Buffer* GetBuffer() const noexcept { return m_Buffer.get(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const noexcept;
		D3D12_CONSTANT_BUFFER_VIEW_DESC MakeCBVDesc() const noexcept;

	private:
		static uint32_t CalcBufferSize() noexcept;

	private:
		std::unique_ptr<DX12Buffer> m_Buffer;
		std::byte* m_MappedData = nullptr;
		uint32_t m_BufferSize = 0;
	};

	template<typename CBType>
	inline DX12ConstantBuffer<CBType>::DX12ConstantBuffer(DX12Device* device) noexcept :
		m_BufferSize(CalcBufferSize())
	{
		m_Buffer = std::make_unique<DX12Buffer>();
		DX12Resource::CreateInfo createInfo = {};
		createInfo.m_Allocator = device->GetMemAllocator();
		createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
		createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		createInfo.m_InitStates = D3D12_RESOURCE_STATE_GENERIC_READ;
		createInfo.m_ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(m_BufferSize);
		m_Buffer->Create(createInfo);

		m_MappedData = static_cast<std::byte*>(m_Buffer->Map());

		std::memset(m_MappedData, 0, m_BufferSize);
	}

	template<typename CBType>
	inline DX12ConstantBuffer<CBType>::~DX12ConstantBuffer()
	{
		if (m_Buffer && m_MappedData)
		{
			m_Buffer->Unmap();
			m_MappedData = nullptr;
		}
	}

	template<typename CBType>
	inline void DX12ConstantBuffer<CBType>::Update(const CBType& data) noexcept
	{
		GGLAB_ASSERT_MSG(m_MappedData, "ConstantBuffer mapped pointer is nullptr.");
		std::memcpy(m_MappedData, &data, sizeof(CBType));
	}

	template<typename CBType>
	inline D3D12_GPU_VIRTUAL_ADDRESS DX12ConstantBuffer<CBType>::GetGPUVirtualAddress() const noexcept
	{
		return m_Buffer ? m_Buffer->GPUVirtualAddress() : 0;
	}

	template<typename CBType>
	inline D3D12_CONSTANT_BUFFER_VIEW_DESC DX12ConstantBuffer<CBType>::MakeCBVDesc() const noexcept
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
		desc.BufferLocation = GetGPUVirtualAddress();
		desc.SizeInBytes = m_BufferSize;
		return desc;
	}

	template<typename CBType>
	inline uint32_t DX12ConstantBuffer<CBType>::CalcBufferSize() noexcept
	{
		return (sizeof(CBType) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
	}
}