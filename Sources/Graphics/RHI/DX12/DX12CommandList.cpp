#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12RootSignature.h"
#include "Graphics/RHI/DX12/DX12PipelineState.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/RHI/DX12/DX12CommandAllocator.h"
#include "Graphics/RHI/DX12/DX12Resource.h"
#include "Core/HResult.h"

namespace gglab
{
	DX12CommandList::DX12CommandList(const CreateInfo& createInfo) noexcept :
		m_DX12Device(createInfo.m_DX12Device),
		m_Type(createInfo.m_Type)
	{
		GGLAB_ASSERT(m_DX12Device);

		GGLAB_HR_DX(
			m_DX12Device->Get()->CreateCommandList1(0,
				m_Type,
				D3D12_COMMAND_LIST_FLAG_NONE,
				IID_PPV_ARGS(&m_D3D12GraphicsCommandList)),
			m_DX12Device->Get());
	}

	void DX12CommandList::Begin(DX12CommandAllocator* allocator) const noexcept
	{
		GGLAB_HR(allocator->Get()->Reset());
		GGLAB_HR(m_D3D12GraphicsCommandList->Reset(allocator->Get(), nullptr));
	}

	void DX12CommandList::End() noexcept
	{
		// TODO: should flush resource barrier here
		FlushBarriers();

		GGLAB_HR(m_D3D12GraphicsCommandList->Close());
	}

	void DX12CommandList::Execute(const DX12CommandQueue& commandQueue) const noexcept
	{
		ID3D12CommandList* const commandLists[] = { m_D3D12GraphicsCommandList.Get() };
		commandQueue.Get()->ExecuteCommandLists(_countof(commandLists), commandLists);
	}

	void DX12CommandList::SetGraphicsRootSignature(const DX12RootSignature& rootSignature) const noexcept
	{
		m_D3D12GraphicsCommandList->SetGraphicsRootSignature(rootSignature.Get());
	}

	void DX12CommandList::SetPipelineState(const DX12PipelineState& pipelineState) const noexcept
	{
		// TODO: for RayTracing change into ID3D12GraphicsCommandList::SetPipelineState1(...).
		m_D3D12GraphicsCommandList->SetPipelineState(pipelineState.Get());
	}

	void DX12CommandList::SetDescriptorHeap(const DX12DescriptorHeap& descriptorHeap) const noexcept
	{
		ID3D12DescriptorHeap* heaps[] = { descriptorHeap.Get() };
		m_D3D12GraphicsCommandList->SetDescriptorHeaps(1, heaps);
	}

	void DX12CommandList::SetDescriptorHeaps(std::span<const DX12DescriptorHeap*> descriptorHeaps) const noexcept
	{
		std::vector<ID3D12DescriptorHeap*> heaps(descriptorHeaps.size());
		for (int32_t index = 0; index < static_cast<int32_t>(descriptorHeaps.size()); ++index)
		{
			GGLAB_ASSERT_NOT_NULL(descriptorHeaps[index]);
			heaps.at(index) = descriptorHeaps[index]->Get();
		}
		m_D3D12GraphicsCommandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
	}

	void DX12CommandList::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const noexcept
	{
		const CD3DX12_VIEWPORT viewport(static_cast<FLOAT>(x), static_cast<FLOAT>(y), static_cast<FLOAT>(width), static_cast<FLOAT>(height));
		m_D3D12GraphicsCommandList->RSSetViewports(1, &viewport);
	}

	void DX12CommandList::SetScissorRect(uint32_t left, uint32_t top, uint32_t width, uint32_t height) const noexcept
	{
		auto right = left + width;
		auto bottom = top + height;
		CD3DX12_RECT scissorRect(static_cast<LONG>(left),
			static_cast<LONG>(top),
			static_cast<LONG>(right),
			static_cast<LONG>(bottom));
		m_D3D12GraphicsCommandList->RSSetScissorRects(1, &scissorRect);
	}

	void DX12CommandList::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology) const noexcept
	{
		m_D3D12GraphicsCommandList->IASetPrimitiveTopology(topology);
	}

	void DX12CommandList::SetRenderTargets(std::span<DX12DescriptorView> rtDescriptors) const noexcept
	{
		SetRenderTargets(rtDescriptors, {});
	}

	void DX12CommandList::SetRenderTargets(std::span<DX12DescriptorView> rtDescriptors, const DX12DescriptorView& dsDescriptor) const noexcept
	{
		const UINT rtCount = static_cast<UINT>(rtDescriptors.size());

		GGLAB_ASSERT_MSG(rtCount <= D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT, "Too many RenderTargets.");

		std::vector<CD3DX12_CPU_DESCRIPTOR_HANDLE> rtHandles(rtCount);
		for (int32_t index = 0; index < static_cast<int32_t>(rtCount); ++index)
		{
			GGLAB_ASSERT(rtDescriptors[index].IsValid());
			rtHandles.at(index) = (rtDescriptors[index].m_CpuHandle);
		}

		const D3D12_CPU_DESCRIPTOR_HANDLE* dsHandlePtr = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE dsHandle{};
		if (dsDescriptor.IsValid())
		{
			dsHandle = dsDescriptor.m_CpuHandle;
			dsHandlePtr = &dsHandle;
		}

		m_D3D12GraphicsCommandList->OMSetRenderTargets(rtCount, rtHandles.data(), FALSE, dsHandlePtr);
	}

	void DX12CommandList::SetRenderTarget(const DX12DescriptorView& rtDsescriptor, const DX12DescriptorView& dsDescriptor) const noexcept
	{
		DX12DescriptorView rtDescriptors[] =
		{
			rtDsescriptor
		};
		SetRenderTargets(rtDescriptors, dsDescriptor);
	}

	void DX12CommandList::SetVertexBuffers(uint32_t startSlot, std::span<D3D12_VERTEX_BUFFER_VIEW> vertexBufferViews) const noexcept
	{
		m_D3D12GraphicsCommandList->IASetVertexBuffers(startSlot, static_cast<UINT>(vertexBufferViews.size()), vertexBufferViews.data());
	}

	void DX12CommandList::SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& indexBufferView) const noexcept
	{
		m_D3D12GraphicsCommandList->IASetIndexBuffer(&indexBufferView);
	}

	void DX12CommandList::SetGraphicsConstantBuffer(uint32_t parameterIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress) const noexcept
	{
		m_D3D12GraphicsCommandList->SetGraphicsRootConstantBufferView(parameterIndex, gpuAddress);
	}

	void DX12CommandList::SetGraphicsDescriptor(uint32_t parameterIndex, const DX12DescriptorView& descriptor) const noexcept
	{
		m_D3D12GraphicsCommandList->SetGraphicsRootDescriptorTable(parameterIndex, descriptor.m_GpuHandle);
	}

	void DX12CommandList::SetGraphicsRoot32BitConstant(uint32_t parameterIndex, uint32_t value, uint32_t destOffset) const noexcept
	{
		m_D3D12GraphicsCommandList->SetGraphicsRoot32BitConstant(parameterIndex, value, destOffset);
	}

	void DX12CommandList::SetGraphicsRoot32BitConstants(uint32_t parameterIndex,
		std::span<const uint32_t> values,
		uint32_t destOffset) const noexcept
	{
		m_D3D12GraphicsCommandList->SetGraphicsRoot32BitConstants(
			parameterIndex,
			static_cast<UINT>(values.size()),
			values.data(),
			destOffset);
	}

	void DX12CommandList::AddTextureBarrier(const CD3DX12_TEXTURE_BARRIER& textureBarrier) noexcept
	{
		m_TextureBarriers.push_back(textureBarrier);
	}

	void DX12CommandList::AddBufferBarrier(const CD3DX12_BUFFER_BARRIER& bufferBarrier) noexcept
	{
		m_BufferBarriers.push_back(bufferBarrier);
	}

	void DX12CommandList::AddGlobalBarrier(const CD3DX12_GLOBAL_BARRIER& globalBarrier) noexcept
	{
		m_GlobalBarriers.push_back(globalBarrier);
	}

	void DX12CommandList::FlushBarriers() noexcept
	{
		std::vector<CD3DX12_BARRIER_GROUP> barrierGroups;
		if (!m_TextureBarriers.empty())
		{
			CD3DX12_BARRIER_GROUP group(static_cast<UINT32>(m_TextureBarriers.size()), m_TextureBarriers.data());
			barrierGroups.push_back(group);
		}

		if (!m_BufferBarriers.empty())
		{
			CD3DX12_BARRIER_GROUP group(static_cast<UINT32>(m_BufferBarriers.size()), m_BufferBarriers.data());
			barrierGroups.push_back(group);
		}

		if (!m_GlobalBarriers.empty())
		{
			CD3DX12_BARRIER_GROUP group(static_cast<UINT32>(m_GlobalBarriers.size()), m_GlobalBarriers.data());
			barrierGroups.push_back(group);
		}

		if (!barrierGroups.empty())
		{
			m_D3D12GraphicsCommandList->Barrier(static_cast<UINT32>(barrierGroups.size()), barrierGroups.data());
		}

		m_TextureBarriers.clear();
		m_BufferBarriers.clear();
		m_GlobalBarriers.clear();
	}

	void DX12CommandList::ClearRenderTarget(const DX12DescriptorView& rtDescriptor, const Color& color) const noexcept
	{
		GGLAB_ASSERT(rtDescriptor.m_DebugType == D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		if (!rtDescriptor.IsValid())
		{
			return;
		}

		float clearColor[4] = { color.R(), color.G(), color.B(), color.A() };
		m_D3D12GraphicsCommandList->ClearRenderTargetView(
			rtDescriptor.m_CpuHandle,
			clearColor,
			0,
			nullptr);
	}

	void DX12CommandList::ClearRenderTarget(const DX12DescriptorView& rtDescriptor, const DX12Resource& resource) const noexcept
	{
		GGLAB_ASSERT(rtDescriptor.m_DebugType == D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		if (!rtDescriptor.IsValid())
		{
			return;
		}

		const auto* clearValue = resource.GetClearValue();
		if (!clearValue)
		{
			return;
		}

		m_D3D12GraphicsCommandList->ClearRenderTargetView(
			rtDescriptor.m_CpuHandle,
			clearValue->Color,
			0,
			nullptr);
	}

	void DX12CommandList::ClearDepthStencil(const DX12DescriptorView& dsDescriptor,
		float depthClearValue, std::optional<uint8_t> stencilClearValue) const noexcept
	{
		GGLAB_ASSERT(dsDescriptor.m_DebugType == D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		if (!dsDescriptor.IsValid())
		{
			return;
		}

		uint8_t stencilValue = 0;
		D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;
		if (stencilClearValue.has_value())
		{
			stencilValue = stencilClearValue.value();
			flags |= D3D12_CLEAR_FLAG_STENCIL;
		}
		m_D3D12GraphicsCommandList->ClearDepthStencilView(dsDescriptor.m_CpuHandle, flags, depthClearValue, stencilValue, 0, nullptr);
	}

	void DX12CommandList::DrawIndexedInstanced(uint32_t indexCount) const noexcept
	{
		m_D3D12GraphicsCommandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
	}

	void DX12CommandList::DrawInstanced(uint32_t vertexCount) const noexcept
	{
		m_D3D12GraphicsCommandList->DrawInstanced(vertexCount, 1, 0, 0);
	}
}
