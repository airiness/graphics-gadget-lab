#include "Precompiled.h"
#include "DX12SwapChain.h"
#include "Application.h"
#include "DX12Device.h"
#include "DX12CommandQueue.h"
#include "DX12CommandList.h"
#include "DX12Descriptor.h"
#include "DX12Texture.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12SwapChain::DX12SwapChain(DX12Device* dx12Device,
		DX12CommandQueue* dx12CommandQueue,
		uint32_t width, uint32_t height,
		DXGI_FORMAT bufferFormat) noexcept :
		m_DX12Device(dx12Device),
		m_DX12CommandQueue(dx12CommandQueue),
		m_Width(width),
		m_Height(height),
		m_Format(bufferFormat),
		m_DxgiSwapChain(CreateSwapChain())
	{
		m_BackBufferIndex = m_DxgiSwapChain->GetCurrentBackBufferIndex();
		CreateRTVs();
		CreateSyncObjects();
	}

	DX12SwapChain::~DX12SwapChain() noexcept
	{
	}

	void DX12SwapChain::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_Width = width;
		m_Height = height;

		// TODO: Check Sync here

		DXGI_SWAP_CHAIN_DESC desc = {};
		utility::ThrowIfFailed(m_DxgiSwapChain->GetDesc(&desc));

		utility::ThrowIfFailed(m_DxgiSwapChain->ResizeBuffers(desc.BufferCount, m_Width, m_Height, desc.BufferDesc.Format, desc.Flags));

		CreateRTVs();
	}

	void DX12SwapChain::SetClearColor(float r, float g, float b, float a) noexcept
	{
		m_ClearColor[0] = r;
		m_ClearColor[1] = g;
		m_ClearColor[2] = b;
		m_ClearColor[3] = a;
	}

	void DX12SwapChain::WaitFrameCompletion() noexcept
	{
		auto& currentFencePoint = m_SyncObjects[m_BackBufferIndex];
		currentFencePoint.Wait();
	}

	void DX12SwapChain::UpdateFrameSyncObject(DX12FencePoint&& fencePoint) noexcept
	{
		m_SyncObjects[m_BackBufferIndex] = fencePoint;
	}

	void DX12SwapChain::Present() noexcept
	{
		m_DxgiSwapChain->Present(1, 0);

		m_BackBufferIndex = (m_BackBufferIndex + 1) % DX12Device::GetBufferCount();
	}

	const DX12Descriptor& DX12SwapChain::GetBackBufferDescriptor(int32_t bufferIndex) const noexcept
	{
		return m_BackBufferDescriptors[bufferIndex];
	}

	DX12Texture* DX12SwapChain::GetCurrentBackBuffer() const noexcept
	{
		return m_BackBuffers.at(m_BackBufferIndex).get();
	}

	void DX12SwapChain::PrepareBackBuffer(DX12CommandList* commandList) const noexcept
	{
		CD3DX12_TEXTURE_BARRIER barrier(
			D3D12_BARRIER_SYNC_ALL,
			D3D12_BARRIER_SYNC_RENDER_TARGET,
			D3D12_BARRIER_ACCESS_COMMON,
			D3D12_BARRIER_ACCESS_RENDER_TARGET,
			D3D12_BARRIER_LAYOUT_PRESENT,
			D3D12_BARRIER_LAYOUT_RENDER_TARGET,
			GetCurrentBackBuffer()->Get(),
			CD3DX12_BARRIER_SUBRESOURCE_RANGE(0));

		commandList->AddTextureBarrier(barrier);
	}

	void DX12SwapChain::FinishBackBuffer(DX12CommandList* commandList) const noexcept
	{
		CD3DX12_TEXTURE_BARRIER barrier(
			D3D12_BARRIER_SYNC_RENDER_TARGET,
			D3D12_BARRIER_SYNC_ALL,
			D3D12_BARRIER_ACCESS_RENDER_TARGET,
			D3D12_BARRIER_ACCESS_COMMON,
			D3D12_BARRIER_LAYOUT_RENDER_TARGET,
			D3D12_BARRIER_LAYOUT_PRESENT,
			GetCurrentBackBuffer()->Get(),
			CD3DX12_BARRIER_SUBRESOURCE_RANGE(0));

		commandList->AddTextureBarrier(barrier);
	}

	void DX12SwapChain::ClearBackBuffer(DX12CommandList* commandList) const noexcept
	{
		commandList->ClearRenderTarget(GetBackBufferDescriptor(GetCurrentBackBufferIndex()), m_ClearColor);
	}

	ComPtr<IDXGISwapChain4> DX12SwapChain::CreateSwapChain() noexcept
	{
		auto hWnd = Application::GetInstance()->GetHwnd();
		auto factory = m_DX12Device->GetDXGIFactory();
		auto isTearingSupport = m_DX12Device->SupportTearing();
		const auto bufferCount = DX12Device::GetBufferCount();

		ComPtr<IDXGISwapChain1> swapChain1;
		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.Width = m_Width;
		desc.Height = m_Height;
		desc.Format = m_Format;
		desc.Stereo = FALSE;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = bufferCount;
		desc.Scaling = DXGI_SCALING_STRETCH;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		desc.Flags = isTearingSupport ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		utility::ThrowIfFailed(factory->CreateSwapChainForHwnd(
			m_DX12CommandQueue->Get(),
			hWnd,
			&desc,
			nullptr,
			nullptr,
			&swapChain1));

		// Invalid Alet+Enter Fullscreen
		utility::ThrowIfFailed(factory->MakeWindowAssociation(hWnd,
			DXGI_MWA_NO_ALT_ENTER |
			DXGI_MWA_NO_WINDOW_CHANGES));	// TODO: support window changes

		ComPtr<IDXGISwapChain4> swapChain4;
		utility::ThrowIfFailed(swapChain1.As(&swapChain4));

		return swapChain4;
	}

	void DX12SwapChain::CreateRTVs() noexcept
	{
		m_BackBuffers.clear();	// TODO: Clear backbuffers in Resize()
		m_BackBufferDescriptors.clear();

		const auto bufferCount = DX12Device::GetBufferCount();

		// Create Descriptor Heap	
		auto device = m_DX12Device->Get();
		auto rtHeap = m_DX12Device->GetRtvDescriptorHeap();

		m_BackBuffers.resize(bufferCount);
		m_BackBufferDescriptors.resize(bufferCount);
		for (uint32_t i = 0; i < bufferCount; ++i)
		{
			ComPtr<ID3D12Resource> backBuffer;
			utility::ThrowIfFailed(m_DxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

			auto descriptor = rtHeap->CreateDescriptor();
			device->CreateRenderTargetView(backBuffer.Get(), nullptr, descriptor.m_CpuHandle);
			m_BackBufferDescriptors[i] = std::move(descriptor);

			auto& tex = m_BackBuffers[i];
			tex = std::make_unique<DX12Texture>();
			tex->CreateFromSwapChain(backBuffer);

#if defined (BUILD_DEBUG)
			utility::SetDebugName(m_BackBuffers[i].get()->Get(), std::format(L"SwapChainBuffer[{:p}]_{}, ", (void*)this, i).c_str());
#endif
		}
	}

	void DX12SwapChain::CreateSyncObjects() noexcept
	{
		const auto bufferCount = DX12Device::GetBufferCount();
		m_SyncObjects.reserve(bufferCount);

		for (int32_t bufferIndex = 0; bufferIndex < static_cast<int32_t>(bufferCount); bufferIndex++)
		{
			m_SyncObjects.push_back(DX12FencePoint());
		}
	}
}
