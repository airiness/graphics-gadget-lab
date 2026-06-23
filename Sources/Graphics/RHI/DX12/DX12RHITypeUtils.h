#pragma once
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHITexture.h"

#include <d3dx12.h>
#include <dxgi1_6.h>

#include <optional>

namespace gglab
{
	[[nodiscard]] DXGI_FORMAT ToDXGIFormat(RHIFormat format) noexcept;
	[[nodiscard]] RHIFormat ToRHIFormat(DXGI_FORMAT format) noexcept;

	[[nodiscard]] D3D12_COMMAND_LIST_TYPE ToD3D12CommandListType(RHIQueueType queueType) noexcept;

	[[nodiscard]] D3D12_BARRIER_ACCESS ToD3D12BarrierAccess(RHIAccess access) noexcept;
	[[nodiscard]] D3D12_BARRIER_LAYOUT ToD3D12BarrierLayout(RHILayout layout) noexcept;
	[[nodiscard]] D3D12_RESOURCE_STATES ToD3D12ResourceStates(RHIAccess access) noexcept;
	[[nodiscard]] D3D12_RESOURCE_STATES ToD3D12ResourceStates(RHIResourceState state) noexcept;

	[[nodiscard]] D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(RHITextureUsage usage) noexcept;
	[[nodiscard]] D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(RHIBufferUsage usage) noexcept;

	[[nodiscard]] std::optional<D3D12_CLEAR_VALUE> ToD3D12ClearValue(const std::optional<RHIClearValue>& clearValue) noexcept;
	[[nodiscard]] D3D12_CLEAR_VALUE ToD3D12ClearValue(const RHIClearValue& clearValue) noexcept;

	[[nodiscard]] CD3DX12_RESOURCE_DESC ToD3D12ResourceDesc(const RHITextureDesc& desc) noexcept;
	[[nodiscard]] CD3DX12_RESOURCE_DESC ToD3D12ResourceDesc(const RHIBufferDesc& desc) noexcept;
}
