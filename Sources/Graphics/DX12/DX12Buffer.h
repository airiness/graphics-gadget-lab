#pragma once
#include "Graphics/DX12/DX12Resource.h"
namespace gglab
{
	class DX12Buffer : public DX12Resource
	{
	public:
		DX12Buffer() noexcept = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12Buffer);
		virtual ~DX12Buffer() = default;

		void* Map(uint32_t subResource = 0, const D3D12_RANGE* readRange = nullptr);
		void Unmap(uint32_t eubResource = 0, const D3D12_RANGE* writtenRange = nullptr);

		uint64_t SizeInBytes() const noexcept;
		D3D12_GPU_VIRTUAL_ADDRESS GPUVirtualAddress() const noexcept;

		static CreateInfo UploadBufferCreateInfo(D3D12MA::Allocator* allocator, uint64_t sizeInBytes) noexcept;
		static CreateInfo VertexOrIndexBufferCreateInfo(D3D12MA::Allocator* allocator, uint64_t sizeInBytes) noexcept;
	};
}