#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12PipelineState.h"
#include "Graphics/RHI/DX12/DX12RootSignature.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorTypes.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"

#include <algorithm>
#include <atomic>

namespace gglab
{
	RHICommandContextHandle AllocateDX12CommandContextHandle() noexcept
	{
		static std::atomic<RHICommandContextHandle::IndexType> nextIndex = 0;
		return RHICommandContextHandle(nextIndex.fetch_add(1, std::memory_order_relaxed), 1);
	}

	DX12CommandContext::DX12CommandContext(DX12Device* device,
		DX12CommandList* commandList,
		RHIQueueType queueType) noexcept :
		m_Handle(AllocateDX12CommandContextHandle()),
		m_Device(device),
		m_CommandList(commandList),
		m_QueueType(queueType)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "DX12CommandContext requires a valid DX12Device.");
		GGLAB_ASSERT_MSG(m_CommandList != nullptr, "DX12CommandContext requires a valid DX12CommandList.");
	}

	ID3D12GraphicsCommandList* DX12CommandContext::Get() const noexcept
	{
		return m_CommandList ? m_CommandList->Get() : nullptr;
	}

	void DX12CommandContext::ClearTrackedResourceUses() noexcept
	{
		m_UsedBuffers.clear();
		m_UsedTextures.clear();
	}

	void DX12CommandContext::TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept
	{
		if (!m_Device || !m_CommandList || barriers.empty())
		{
			return;
		}

		std::vector<CD3DX12_RESOURCE_BARRIER> nativeBarriers;
		nativeBarriers.reserve(barriers.size());
		for (const RHITextureBarrier& barrier : barriers)
		{
			DX12Texture* texture = m_Device->ResolveTexture(barrier.m_Texture);
			if (!texture)
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12CommandContext::TextureBarrier received a non-live texture handle.");
				continue;
			}

			nativeBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
				texture->Get(),
				ToD3D12ResourceStates(barrier.m_Before),
				ToD3D12ResourceStates(barrier.m_After)));
			TrackTextureUse(barrier.m_Texture);
		}

		if (!nativeBarriers.empty())
		{
			m_CommandList->Get()->ResourceBarrier(
				static_cast<UINT>(nativeBarriers.size()),
				nativeBarriers.data());
		}
	}

	void DX12CommandContext::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
	{
		if (!m_Device || !m_CommandList || barriers.empty())
		{
			return;
		}

		std::vector<CD3DX12_RESOURCE_BARRIER> nativeBarriers;
		nativeBarriers.reserve(barriers.size());
		for (const RHIBufferBarrier& barrier : barriers)
		{
			DX12Buffer* buffer = m_Device->ResolveBuffer(barrier.m_Buffer);
			if (!buffer)
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12CommandContext::BufferBarrier received a non-live buffer handle.");
				continue;
			}

			nativeBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
				buffer->Get(),
				ToD3D12ResourceStates(barrier.m_Before),
				ToD3D12ResourceStates(barrier.m_After)));
			TrackBufferUse(barrier.m_Buffer);
		}

		if (!nativeBarriers.empty())
		{
			m_CommandList->Get()->ResourceBarrier(
				static_cast<UINT>(nativeBarriers.size()),
				nativeBarriers.data());
		}
	}

	void DX12CommandContext::TrackBufferUse(RHIBufferHandle buffer) noexcept
	{
		if (std::ranges::find(m_UsedBuffers, buffer) == m_UsedBuffers.end())
		{
			m_UsedBuffers.push_back(buffer);
		}
	}

	void DX12CommandContext::TrackTextureUse(RHITextureHandle texture) noexcept
	{
		if (std::ranges::find(m_UsedTextures, texture) == m_UsedTextures.end())
		{
			m_UsedTextures.push_back(texture);
		}
	}

	DX12GraphicsCommandContext::DX12GraphicsCommandContext(DX12Device* device,
		DX12CommandList* commandList) noexcept :
		m_Backend(device, commandList, RHIQueueType::Graphics)
	{}

	void DX12GraphicsCommandContext::TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept
	{
		m_Backend.TextureBarrier(barriers);
	}

	void DX12GraphicsCommandContext::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
	{
		m_Backend.BufferBarrier(barriers);
	}

	void DX12GraphicsCommandContext::SetPipeline(RHIPipelineHandle pipeline) noexcept
	{
		GGLAB_ASSERT_MSG(!pipeline.IsValid(),
			"DX12GraphicsCommandContext::SetPipeline(RHIPipelineHandle) needs an RHI pipeline table before it can be used.");
	}

	void DX12GraphicsCommandContext::SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept
	{
		GGLAB_ASSERT_MSG(binding.m_TableIndex == 0 && binding.m_ParameterIndex == 0,
			"DX12GraphicsCommandContext::SetDescriptorTable needs an RHI descriptor table allocator before it can be used.");
	}

	void DX12GraphicsCommandContext::SetVertexBuffers(uint32_t startSlot,
		std::span<const RHIVertexBufferBinding> bindings) noexcept
	{
		if (bindings.empty())
		{
			return;
		}

		DX12Device* device = m_Backend.GetDevice();
		GGLAB_ASSERT_NOT_NULL(device);

		std::vector<D3D12_VERTEX_BUFFER_VIEW> nativeBindings;
		nativeBindings.reserve(bindings.size());
		for (const RHIVertexBufferBinding& binding : bindings)
		{
			DX12Buffer* buffer = device->ResolveBuffer(binding.m_Buffer);
			if (!buffer)
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12GraphicsCommandContext::SetVertexBuffers received a non-live buffer handle.");
				continue;
			}

			D3D12_VERTEX_BUFFER_VIEW view{};
			view.BufferLocation = buffer->GPUVirtualAddress() + binding.m_Offset;
			view.StrideInBytes = binding.m_Stride;
			view.SizeInBytes = binding.m_SizeInBytes;
			nativeBindings.push_back(view);
			m_Backend.TrackBufferUse(binding.m_Buffer);
		}

		if (!nativeBindings.empty())
		{
			m_Backend.GetCommandList()->SetVertexBuffers(
				startSlot,
				std::span<D3D12_VERTEX_BUFFER_VIEW>(
					nativeBindings.data(),
					nativeBindings.size()));
		}
	}

	void DX12GraphicsCommandContext::SetIndexBuffer(const RHIIndexBufferBinding& binding) noexcept
	{
		DX12Device* device = m_Backend.GetDevice();
		GGLAB_ASSERT_NOT_NULL(device);

		DX12Buffer* buffer = device->ResolveBuffer(binding.m_Buffer);
		if (!buffer)
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12GraphicsCommandContext::SetIndexBuffer received a non-live buffer handle.");
			return;
		}

		D3D12_INDEX_BUFFER_VIEW view{};
		view.BufferLocation = buffer->GPUVirtualAddress() + binding.m_Offset;
		view.SizeInBytes = binding.m_SizeInBytes;
		view.Format = ToDXGIFormat(binding.m_Format);
		m_Backend.GetCommandList()->SetIndexBuffer(view);
		m_Backend.TrackBufferUse(binding.m_Buffer);
	}

	void DX12GraphicsCommandContext::DrawIndexed(uint32_t indexCount,
		uint32_t instanceCount,
		uint32_t startIndexLocation,
		int32_t baseVertexLocation,
		uint32_t startInstanceLocation) noexcept
	{
		m_Backend.Get()->DrawIndexedInstanced(
			indexCount,
			instanceCount,
			startIndexLocation,
			baseVertexLocation,
			startInstanceLocation);
	}

	void DX12GraphicsCommandContext::SetRootSignature(const DX12RootSignature& rootSignature) noexcept
	{
		m_Backend.GetCommandList()->SetGraphicsRootSignature(rootSignature);
	}

	void DX12GraphicsCommandContext::SetPipelineState(const DX12PipelineState& pipelineState) noexcept
	{
		m_Backend.GetCommandList()->SetPipelineState(pipelineState);
	}

	void DX12GraphicsCommandContext::SetDescriptor(uint32_t parameterIndex,
		const DX12DescriptorView& descriptor) noexcept
	{
		m_Backend.GetCommandList()->SetGraphicsDescriptor(parameterIndex, descriptor);
	}

	DX12ComputeCommandContext::DX12ComputeCommandContext(DX12Device* device,
		DX12CommandList* commandList) noexcept :
		m_Backend(device, commandList, RHIQueueType::Compute)
	{}

	void DX12ComputeCommandContext::TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept
	{
		m_Backend.TextureBarrier(barriers);
	}

	void DX12ComputeCommandContext::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
	{
		m_Backend.BufferBarrier(barriers);
	}

	void DX12ComputeCommandContext::SetPipeline(RHIPipelineHandle pipeline) noexcept
	{
		GGLAB_ASSERT_MSG(!pipeline.IsValid(),
			"DX12ComputeCommandContext::SetPipeline(RHIPipelineHandle) needs an RHI pipeline table before it can be used.");
	}

	void DX12ComputeCommandContext::SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept
	{
		GGLAB_ASSERT_MSG(binding.m_TableIndex == 0 && binding.m_ParameterIndex == 0,
			"DX12ComputeCommandContext::SetDescriptorTable needs an RHI descriptor table allocator before it can be used.");
	}

	void DX12ComputeCommandContext::Dispatch(uint32_t groupCountX,
		uint32_t groupCountY,
		uint32_t groupCountZ) noexcept
	{
		m_Backend.Get()->Dispatch(groupCountX, groupCountY, groupCountZ);
	}

	void DX12ComputeCommandContext::SetPipelineState(const DX12PipelineState& pipelineState) noexcept
	{
		m_Backend.GetCommandList()->SetPipelineState(pipelineState);
	}

	void DX12ComputeCommandContext::SetDescriptor(uint32_t parameterIndex,
		const DX12DescriptorView& descriptor) noexcept
	{
		m_Backend.Get()->SetComputeRootDescriptorTable(parameterIndex, descriptor.m_GpuHandle);
	}
}
