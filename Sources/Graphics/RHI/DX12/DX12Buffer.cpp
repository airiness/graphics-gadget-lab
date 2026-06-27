#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Core/HResult.h"

namespace gglab
{
	void* DX12Buffer::Map(uint32_t subResource, const D3D12_RANGE* readRange)
	{
		void* pointer = nullptr;
		GGLAB_HR(Get()->Map(static_cast<UINT>(subResource), readRange, &pointer));
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
}
