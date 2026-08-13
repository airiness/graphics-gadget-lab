#pragma once
#include "Graphics/RHI/RHIResource.h"

#include <d3d12.h>

namespace gglab
{
	struct RHIBufferBarrier;
	struct RHITextureBarrier;

	[[nodiscard]] D3D12_BARRIER_SYNC ToD3D12BarrierSync(RHIStage stages) noexcept;
	[[nodiscard]] D3D12_BARRIER_ACCESS ToD3D12BarrierAccess(RHIAccess access) noexcept;
	[[nodiscard]] D3D12_BARRIER_LAYOUT ToD3D12BarrierLayout(RHILayout layout) noexcept;
	[[nodiscard]] D3D12_BARRIER_LAYOUT ToD3D12TextureInitialLayout(RHILayout layout) noexcept;
	[[nodiscard]] D3D12_TEXTURE_BARRIER BuildD3D12TextureBarrier(const RHITextureBarrier& barrier,
		ID3D12Resource* resource, const D3D12_RESOURCE_DESC& resourceDesc) noexcept;
	[[nodiscard]] D3D12_BUFFER_BARRIER BuildD3D12BufferBarrier(
		const RHIBufferBarrier& barrier, ID3D12Resource* resource) noexcept;
	[[nodiscard]] D3D12_RESOURCE_STATES ToD3D12ResourceStates(RHIAccess access) noexcept;
	[[nodiscard]] D3D12_RESOURCE_STATES ToD3D12ResourceStates(RHIResourceState state) noexcept;
}
