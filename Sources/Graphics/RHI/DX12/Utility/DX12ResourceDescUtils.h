#pragma once
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHITexture.h"

#include <d3dx12.h>

#include <optional>

namespace gglab
{
	[[nodiscard]] D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(RHITextureUsage usage) noexcept;
	[[nodiscard]] D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(RHIBufferUsage usage) noexcept;

	[[nodiscard]] std::optional<D3D12_CLEAR_VALUE> ToD3D12ClearValue(const std::optional<RHIClearValue>& clearValue) noexcept;
	[[nodiscard]] D3D12_CLEAR_VALUE ToD3D12ClearValue(const RHIClearValue& clearValue) noexcept;

	[[nodiscard]] CD3DX12_RESOURCE_DESC ToD3D12ResourceDesc(const RHITextureDesc& desc) noexcept;
	[[nodiscard]] CD3DX12_RESOURCE_DESC ToD3D12ResourceDesc(const RHIBufferDesc& desc) noexcept;
}
