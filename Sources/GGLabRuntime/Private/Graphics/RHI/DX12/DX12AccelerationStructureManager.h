#pragma once
#include "Graphics/RHI/AccelerationStructureStore.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"

#include <d3d12.h>
#include <optional>

namespace gglab
{
	class DX12Device;
	class DX12AccelerationStructureManager
	{
	public:
		explicit DX12AccelerationStructureManager(DX12Device& device) noexcept : m_Device(device) {}
		[[nodiscard]] std::optional<RHIASBuildSizes> Query(const RHIASBuildInputs& inputs) noexcept;
		[[nodiscard]] RHIAccelerationStructureHandle Create(const RHIAccelerationStructureDesc& desc) noexcept;
		void Destroy(RHIAccelerationStructureHandle handle) noexcept;
		[[nodiscard]] bool IsAlive(RHIAccelerationStructureHandle handle) const noexcept
		{
			return m_Store.Resolve(handle) != nullptr;
		}
		[[nodiscard]] RHIAllocationFacts QueryAllocationFacts(RHIAccelerationStructureHandle handle) const noexcept;
		[[nodiscard]] bool Build(RHICommandContext& context, ID3D12GraphicsCommandList& commands,
			RHIAccelerationStructureHandle destination, const RHIASBuildInputs& inputs,
			const RHIASBufferRange& scratch) noexcept;
		[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS Bind(RHICommandContext& context,
			RHIAccelerationStructureHandle handle) noexcept;
		[[nodiscard]] ID3D12Resource* ResolveResource(RHICommandContext& context,
			RHIAccelerationStructureHandle handle) noexcept;

	private:
		[[nodiscard]] bool Describe(const RHIASBuildInputs& inputs,
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& native,
			D3D12_RAYTRACING_GEOMETRY_DESC& geometry) noexcept;
		DX12Device& m_Device;
		RHIAccelerationStructureStore m_Store;
	};
}
