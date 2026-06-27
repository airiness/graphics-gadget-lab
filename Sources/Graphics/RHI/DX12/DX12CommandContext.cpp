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
	namespace
	{
		D3D12_PRIMITIVE_TOPOLOGY ToD3D12PrimitiveTopology(RHIPrimitiveTopology topology) noexcept
		{
			switch (topology)
			{
			case RHIPrimitiveTopology::PointList: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			case RHIPrimitiveTopology::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
			case RHIPrimitiveTopology::LineStrip: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
			case RHIPrimitiveTopology::TriangleList: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			case RHIPrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
			case RHIPrimitiveTopology::Unknown:
			default: return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			}
		}
	}

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

		for (const RHITextureBarrier& barrier : barriers)
		{
			DX12Texture* texture = m_Device->ResolveTexture(barrier.m_Texture);
			if (!texture)
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12CommandContext::TextureBarrier received a non-live texture handle.");
				continue;
			}

			m_CommandList->AddTextureBarrier(
				CD3DX12_TEXTURE_BARRIER(
					ToD3D12BarrierSync(barrier.m_Before.m_Access),
					ToD3D12BarrierSync(barrier.m_After.m_Access),
					ToD3D12BarrierAccess(barrier.m_Before.m_Access),
					ToD3D12BarrierAccess(barrier.m_After.m_Access),
					ToD3D12BarrierLayout(barrier.m_Before.m_Layout),
					ToD3D12BarrierLayout(barrier.m_After.m_Layout),
					texture->Get(),
					CD3DX12_BARRIER_SUBRESOURCE_RANGE(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)));
			TrackTextureUse(barrier.m_Texture);
		}

		m_CommandList->FlushBarriers();
	}

	void DX12CommandContext::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
	{
		if (!m_Device || !m_CommandList || barriers.empty())
		{
			return;
		}

		for (const RHIBufferBarrier& barrier : barriers)
		{
			DX12Buffer* buffer = m_Device->ResolveBuffer(barrier.m_Buffer);
			if (!buffer)
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12CommandContext::BufferBarrier received a non-live buffer handle.");
				continue;
			}

			m_CommandList->AddBufferBarrier(
				CD3DX12_BUFFER_BARRIER(
					ToD3D12BarrierSync(barrier.m_Before.m_Access),
					ToD3D12BarrierSync(barrier.m_After.m_Access),
					ToD3D12BarrierAccess(barrier.m_Before.m_Access),
					ToD3D12BarrierAccess(barrier.m_After.m_Access),
					buffer->Get()));
			TrackBufferUse(barrier.m_Buffer);
		}

		m_CommandList->FlushBarriers();
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
		DX12PipelineState* pipelineState = nullptr;
		DX12RootSignature* rootSignature = nullptr;
		if (!m_Backend.GetDevice()->ResolveGraphicsPipeline(pipeline, pipelineState, rootSignature))
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12GraphicsCommandContext::SetPipeline received an invalid pipeline handle.");
			return;
		}

		if (m_CurrentRootSignature != rootSignature)
		{
			m_Backend.GetCommandList()->SetGraphicsRootSignature(*rootSignature);
			m_CurrentRootSignature = rootSignature;
		}
		m_Backend.GetCommandList()->SetPipelineState(*pipelineState);
	}

	void DX12GraphicsCommandContext::SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept
	{
		const D3D12_GPU_DESCRIPTOR_HANDLE table = m_Backend.GetDevice()->ResolveShaderVisibleDescriptor(
			binding.m_HeapType,
			binding.m_TableIndex);
		if (table.ptr == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12GraphicsCommandContext::SetDescriptorTable received an invalid descriptor table binding.");
			return;
		}
		m_Backend.Get()->SetGraphicsRootDescriptorTable(binding.m_ParameterIndex, table);
	}

	void DX12GraphicsCommandContext::SetRenderTargets(
		std::span<const RHITextureViewHandle> renderTargets,
		RHITextureViewHandle depthStencil) noexcept
	{
		std::vector<DX12DescriptorView> nativeRenderTargets;
		nativeRenderTargets.reserve(renderTargets.size());
		for (const RHITextureViewHandle view : renderTargets)
		{
			const DX12DescriptorView nativeView = m_Backend.GetDevice()->ResolveTextureView(view);
			if (nativeView.IsValid())
			{
				nativeRenderTargets.push_back(nativeView);
			}
		}

		if (depthStencil.IsValid())
		{
			const DX12DescriptorView nativeDepth = m_Backend.GetDevice()->ResolveTextureView(depthStencil);
			GGLAB_ASSERT_MSG(nativeDepth.IsValid(), "Depth-stencil view must resolve to a DX12 descriptor.");
			m_Backend.GetCommandList()->SetRenderTargets(nativeRenderTargets, nativeDepth);
		}
		else
		{
			m_Backend.GetCommandList()->SetRenderTargets(nativeRenderTargets);
		}
	}

	void DX12GraphicsCommandContext::ClearColor(
		RHITextureViewHandle renderTarget,
		const std::array<float, 4>& color) noexcept
	{
		const DX12DescriptorView view = m_Backend.GetDevice()->ResolveTextureView(renderTarget);
		GGLAB_ASSERT_MSG(view.IsValid(), "Render-target view must resolve to a DX12 descriptor.");
		if (view.IsValid())
		{
			m_Backend.Get()->ClearRenderTargetView(view.m_CpuHandle, color.data(), 0, nullptr);
		}
	}

	void DX12GraphicsCommandContext::ClearDepthStencil(
		RHITextureViewHandle depthStencil,
		float depth,
		std::optional<uint8_t> stencil) noexcept
	{
		const DX12DescriptorView view = m_Backend.GetDevice()->ResolveTextureView(depthStencil);
		GGLAB_ASSERT_MSG(view.IsValid(), "Depth-stencil view must resolve to a DX12 descriptor.");
		if (view.IsValid())
		{
			m_Backend.GetCommandList()->ClearDepthStencil(view, depth, stencil);
		}
	}

	void DX12GraphicsCommandContext::SetViewport(const RHIViewport& viewport) noexcept
	{
		D3D12_VIEWPORT nativeViewport{
			.TopLeftX = viewport.m_X,
			.TopLeftY = viewport.m_Y,
			.Width = viewport.m_Width,
			.Height = viewport.m_Height,
			.MinDepth = viewport.m_MinDepth,
			.MaxDepth = viewport.m_MaxDepth,
		};
		m_Backend.Get()->RSSetViewports(1, &nativeViewport);
	}

	void DX12GraphicsCommandContext::SetScissorRect(const RHIScissorRect& rect) noexcept
	{
		D3D12_RECT nativeRect{ rect.m_Left, rect.m_Top, rect.m_Right, rect.m_Bottom };
		m_Backend.Get()->RSSetScissorRects(1, &nativeRect);
	}

	void DX12GraphicsCommandContext::SetPrimitiveTopology(RHIPrimitiveTopology topology) noexcept
	{
		const D3D12_PRIMITIVE_TOPOLOGY nativeTopology = ToD3D12PrimitiveTopology(topology);
		GGLAB_ASSERT_MSG(nativeTopology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED, "Invalid RHI primitive topology.");
		m_Backend.GetCommandList()->SetPrimitiveTopology(nativeTopology);
	}

	void DX12GraphicsCommandContext::SetConstantBuffer(
		uint32_t parameterIndex,
		RHIBufferHandle buffer,
		uint64_t offset) noexcept
	{
		DX12Buffer* nativeBuffer = m_Backend.GetDevice()->ResolveBuffer(buffer);
		if (!nativeBuffer || offset >= nativeBuffer->SizeInBytes())
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12GraphicsCommandContext::SetConstantBuffer received an invalid buffer binding.");
			return;
		}
		m_Backend.GetCommandList()->SetGraphicsConstantBuffer(
			parameterIndex,
			nativeBuffer->GPUVirtualAddress() + offset);
		m_Backend.TrackBufferUse(buffer);
	}

	void DX12GraphicsCommandContext::SetReadOnlyBuffer(
		uint32_t parameterIndex,
		RHIBufferHandle buffer,
		uint64_t offset) noexcept
	{
		DX12Buffer* nativeBuffer = m_Backend.GetDevice()->ResolveBuffer(buffer);
		if (!nativeBuffer || offset >= nativeBuffer->SizeInBytes())
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12GraphicsCommandContext::SetReadOnlyBuffer received an invalid buffer binding.");
			return;
		}
		m_Backend.Get()->SetGraphicsRootShaderResourceView(
			parameterIndex,
			nativeBuffer->GPUVirtualAddress() + offset);
		m_Backend.TrackBufferUse(buffer);
	}

	void DX12GraphicsCommandContext::SetPushConstants(
		uint32_t parameterIndex,
		std::span<const uint32_t> values,
		uint32_t destOffset) noexcept
	{
		m_Backend.GetCommandList()->SetGraphicsRoot32BitConstants(parameterIndex, values, destOffset);
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

	void DX12GraphicsCommandContext::Draw(
		uint32_t vertexCount,
		uint32_t instanceCount,
		uint32_t startVertexLocation,
		uint32_t startInstanceLocation) noexcept
	{
		m_Backend.Get()->DrawInstanced(
			vertexCount,
			instanceCount,
			startVertexLocation,
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
