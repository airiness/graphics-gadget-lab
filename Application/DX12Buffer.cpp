#include "Precompiled.h"
#include "DX12Buffer.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	void DX12Buffer::CreateBuffer(D3D12MA::Allocator& allocator,
		uint64_t sizeInBytes,
		D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES initStates,
		D3D12_HEAP_TYPE heapType,
		D3D12MA::ALLOCATION_FLAGS allocFlags) noexcept
	{
		CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT>(sizeInBytes), flags);

		CreateInfo createInfo = {};
		createInfo.m_Allocator = &allocator;
		createInfo.m_AllocDesc.HeapType = heapType;
		createInfo.m_AllocDesc.Flags = allocFlags;
		createInfo.m_InitStates = initStates;
		createInfo.m_ResourceDesc = resourceDesc;

		Create(createInfo);
	}

	void* DX12Buffer::Map(uint32_t subResource, const D3D12_RANGE* readRange)
	{
		void* pointer = nullptr;
		utility::ThrowIfFailed(Get()->Map(static_cast<UINT>(subResource), readRange, &pointer));
		return pointer;
	}

	void DX12Buffer::Unmap(uint32_t subResource, const D3D12_RANGE* writtenRange)
	{
		Get()->Unmap(static_cast<UINT>(subResource), writtenRange);
	}

	uint64_t DX12Buffer::SizeInBytes() const noexcept
	{
		return m_ResourceDesc.Width;
	}

	D3D12_GPU_VIRTUAL_ADDRESS DX12Buffer::GPUVirtualAddress() const noexcept
	{
		return IsValid() ? m_Resource->GetGPUVirtualAddress() : 0;
	}

	DX12Resource::CreateInfo DX12Buffer::UploadBufferCreateInfo(D3D12MA::Allocator* allocator, uint64_t sizeInBytes) noexcept
	{
		GGLAB_ASSERT_MSG(allocator != nullptr, "Allocator can not be null.");

		CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(sizeInBytes));

		CreateInfo createInfo = {};
		createInfo.m_Allocator = allocator;
		createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
		createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		createInfo.m_InitStates = D3D12_RESOURCE_STATE_GENERIC_READ;
		createInfo.m_ResourceDesc = resourceDesc;

		return createInfo;
	}
}
