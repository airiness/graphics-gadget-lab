#include "Precompiled.h"
#include "DX12CommandList.h"
#include "DX12CommandQueue.h"
#include "DX12Device.h"
#include "DX12RootSignature.h"
#include "DX12PipelineState.h"
#include "DX12Descriptor.h"
#include "DX12CommandAllocator.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12CommandList::DX12CommandList(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept :
		m_DX12Device(dx12Device),
		m_Type(type)
	{
		CreateCommandList(type);
	}

	DX12CommandList::~DX12CommandList() noexcept
	{
	}

	void DX12CommandList::Begin(DX12CommandAllocator* allocator) noexcept
	{
		utility::ThrowIfFailed(allocator->Get()->Reset());
		utility::ThrowIfFailed(m_D3D12GraphicsCommandList->Reset(allocator->Get(), nullptr));
	}

	void DX12CommandList::End() noexcept
	{
		// TODO: should flush resource barrier here
		FlushBarriers();

		utility::ThrowIfFailed(m_D3D12GraphicsCommandList->Close());
	}

	void DX12CommandList::Execute(const DX12CommandQueue& commandQueue) noexcept
	{
		ID3D12CommandList* const commandLists[] = { m_D3D12GraphicsCommandList.Get() };
		commandQueue.Get()->ExecuteCommandLists(_countof(commandLists), commandLists);
	}

	void DX12CommandList::SetGraphicsRootSignature(const DX12RootSignature& rootSignature) noexcept
	{
		m_D3D12GraphicsCommandList->SetGraphicsRootSignature(rootSignature.Get());
	}

	void DX12CommandList::SetPipelineState(const DX12PipelineState& pipelineState) noexcept
	{
		// TODO: for RayTracing change into ID3D12GraphicsCommandList::SetPipelineState1(...).
		m_D3D12GraphicsCommandList->SetPipelineState(pipelineState.Get());
	}

	void DX12CommandList::SetDescriptorHeap(const DX12DescriptorHeap& descriptorHeap) noexcept
	{
		ID3D12DescriptorHeap* heaps[] = { descriptorHeap.Get() };
		m_D3D12GraphicsCommandList->SetDescriptorHeaps(1, heaps);
	}

	void DX12CommandList::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) noexcept
	{
		CD3DX12_VIEWPORT viewport(static_cast<FLOAT>(x), static_cast<FLOAT>(y), static_cast<FLOAT>(width), static_cast<FLOAT>(height));
		m_D3D12GraphicsCommandList->RSSetViewports(1, &viewport);
	}

	void DX12CommandList::SetScissorRect(uint32_t left, uint32_t top, uint32_t width, uint32_t height) noexcept
	{
		auto right = left + width;
		auto bottom = top + height;
		CD3DX12_RECT scissorRect(left, top, right, bottom);
		m_D3D12GraphicsCommandList->RSSetScissorRects(1, &scissorRect);
	}

	void DX12CommandList::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology) noexcept
	{
		m_D3D12GraphicsCommandList->IASetPrimitiveTopology(topology);
	}

	void DX12CommandList::SetRenderTargets(std::span<DX12Descriptor> rtDescriptors, DX12Descriptor* dsDescriptor) noexcept
	{
		auto rtCount = rtDescriptors.size();

		std::vector<CD3DX12_CPU_DESCRIPTOR_HANDLE> rtHandles(rtCount);
		for (int32_t index = 0; index < rtCount; ++index)
		{
			rtHandles.at(index) = (rtDescriptors[index].m_CpuHandle);
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE* dsHandle = nullptr;
		if (dsDescriptor)
		{
			dsHandle = &dsDescriptor->m_CpuHandle;
		}

		m_D3D12GraphicsCommandList->OMSetRenderTargets(static_cast<UINT>(rtCount), rtHandles.data(), FALSE, dsHandle);
	}

	void DX12CommandList::SetVertexBuffers(uint32_t startSlot, std::span<D3D12_VERTEX_BUFFER_VIEW> vertexBufferViews) noexcept
	{
		m_D3D12GraphicsCommandList->IASetVertexBuffers(startSlot, static_cast<UINT>(vertexBufferViews.size()), vertexBufferViews.data());
	}

	void DX12CommandList::SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& indexBufferView) noexcept
	{
		m_D3D12GraphicsCommandList->IASetIndexBuffer(&indexBufferView);
	}

	void DX12CommandList::SetGraphicsConstantBuffer(uint32_t parameterIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress) noexcept
	{
		m_D3D12GraphicsCommandList->SetGraphicsRootConstantBufferView(static_cast<UINT>(parameterIndex), gpuAddress);
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

	void DX12CommandList::ClearRenderTarget(DX12Descriptor rtDescriptor, const float* clearColor) noexcept
	{
		m_D3D12GraphicsCommandList->ClearRenderTargetView(rtDescriptor.m_CpuHandle, clearColor, 0, nullptr);
	}

	void DX12CommandList::ClearDepthStencil(DX12Descriptor dsDescriptor, float depthClearValue, std::optional<uint8_t> stencilClearValue) noexcept
	{
		uint8_t stencilValue = 0;
		D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;
		if (stencilClearValue.has_value())
		{
			stencilValue = stencilClearValue.value();
			flags |= D3D12_CLEAR_FLAG_STENCIL;
		}
		m_D3D12GraphicsCommandList->ClearDepthStencilView(dsDescriptor.m_CpuHandle, flags, depthClearValue, stencilValue, 0, nullptr);
	}

	void DX12CommandList::DrawIndexedInstanced(uint32_t indexCount) noexcept
	{
		m_D3D12GraphicsCommandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
	}

	void DX12CommandList::DrawInstanced(uint32_t vertexCount) noexcept
	{
		m_D3D12GraphicsCommandList->DrawInstanced(vertexCount, 1, 0, 0);
	}

	void DX12CommandList::CreateCommandList(D3D12_COMMAND_LIST_TYPE type) noexcept
	{
		auto device = m_DX12Device->Get();
		utility::ThrowIfFailed(device->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE,
			IID_PPV_ARGS(&m_D3D12GraphicsCommandList)));

#if defined (BUILD_DEBUG)
		utility::SetDebugName(m_D3D12GraphicsCommandList.Get(),
			std::format(L"CommandList[{:p}]_{} ", (void*)this, utility::GetCommandListTypeName(type)).c_str());
#endif

		// CommandList created by CreateCommandList1 is already closed.
		//utility::ThrowIfFailed(m_D3D12GraphicsCommandList->Close());
	}
}