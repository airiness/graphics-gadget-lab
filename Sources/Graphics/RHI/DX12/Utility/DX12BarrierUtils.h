#pragma once
#include "Graphics/RHI/RHIResource.h"

#include <d3d12.h>

namespace gglab
{
	[[nodiscard]] D3D12_BARRIER_SYNC ToD3D12BarrierSync(RHIStage stages) noexcept;
	[[nodiscard]] D3D12_BARRIER_ACCESS ToD3D12BarrierAccess(RHIAccess access) noexcept;
	[[nodiscard]] D3D12_BARRIER_LAYOUT ToD3D12BarrierLayout(RHILayout layout) noexcept;
	[[nodiscard]] D3D12_RESOURCE_STATES ToD3D12ResourceStates(RHIAccess access) noexcept;
	[[nodiscard]] D3D12_RESOURCE_STATES ToD3D12ResourceStates(RHIResourceState state) noexcept;
}
