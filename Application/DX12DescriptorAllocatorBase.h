#pragma once
#include "DX12DescriptorTypes.h"
#include "DX12FencePoint.h"
#include "AllocatorBase.h"

namespace gglab
{
	class DX12DescriptorHeap;
	class DX12DescriptorAllocatorBase
	{
	public:
		struct CreateInfo
		{
			DX12DescriptorHeap* m_DescriptorHeap = nullptr;
			DX12DescriptorRange m_Range{};
		};

	public:
		explicit DX12DescriptorAllocatorBase(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorAllocatorBase);
		virtual ~DX12DescriptorAllocatorBase() = default;

		virtual void Tick() noexcept {}
		virtual void EndFrame(const DX12FencePoint& fencePoint) noexcept {}

		DX12DescriptorHeap* GetHeap() const noexcept { return m_DescriptorHeap; }
		const DX12DescriptorRange& Range() const noexcept { return m_Range; }

		D3D12_DESCRIPTOR_HEAP_TYPE HeapType() const noexcept { return m_Type; }
		bool IsShaderVisible() const noexcept { return (m_Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0; }
		uint32_t IncrementSize() const noexcept { return m_IncrementSize; }

		uint32_t ToLocalIndex(uint32_t globalIndex) const noexcept;
		uint32_t ToGlobalIndex(uint32_t localIndex) const noexcept;

		DX12DescriptorSpan ToLocalSpan(const DX12DescriptorSpan& globalSpan) const noexcept;
		DX12DescriptorSpan ToGlobalSpan(const DX12DescriptorSpan& localSpan) const noexcept;

		uint32_t GlobalIndexFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) const noexcept;
		uint32_t GlobalIndexFromGpuHandle(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) const noexcept;

		CD3DX12_CPU_DESCRIPTOR_HANDLE CpuHandleAtGlobalIndex(uint32_t globalIndex) const noexcept;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandleAtGlobalIndex(uint32_t globalIndex) const noexcept;

	protected:
		DX12DescriptorHandle CreateHandleFromGlobalSpan(const DX12DescriptorSpan& globalSpan, uint32_t generation) noexcept;

		virtual void FreeHandleInternal(DX12DescriptorHandle& descriptorHandle) noexcept = 0;
		virtual void RetireHandleInternal(const DX12DescriptorHandle& descriptorHandle, const DX12FencePoint& fencePoint) noexcept = 0;

	protected:
		DX12DescriptorHeap* m_DescriptorHeap;
		DX12DescriptorRange m_Range{};

		D3D12_DESCRIPTOR_HEAP_TYPE m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		D3D12_DESCRIPTOR_HEAP_FLAGS m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		CD3DX12_CPU_DESCRIPTOR_HANDLE m_CpuStart{};
		CD3DX12_GPU_DESCRIPTOR_HANDLE m_GpuStart{};

		uint32_t m_IncrementSize = 0;

		friend class DX12DescriptorHandle;
	};
}