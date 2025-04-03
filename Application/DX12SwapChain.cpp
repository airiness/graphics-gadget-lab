#include "Precompiled.h"
#include "DX12SwapChain.h"
#include "Utility.h"
#include "Application.h"

namespace graphicsGadgetLab
{
	DX12SwapChain::DX12SwapChain(DX12Device* dx12Device, 
		DX12CommandQueue* dx12CommandQueue,
		uint32_t width, uint32_t height, DXGI_FORMAT bufferFormat) noexcept :
		m_DX12Device(dx12Device),
		m_DX12CommandQueue(dx12CommandQueue),
		m_Width(width),
		m_Height(height),
		m_Format(bufferFormat),
		m_DxgiSwapChain(CreateSwapChain())
	{
		CreateRTVs();
	}

	DX12SwapChain::~DX12SwapChain() noexcept
	{
	}

	ComPtr<IDXGISwapChain4> DX12SwapChain::CreateSwapChain() noexcept
	{
		auto hWnd = Application::Get()->GetHwnd();
		auto factory = m_DX12Device->GetDXGIFactory();
		auto isTearingSupport = m_DX12Device->SupportTearing();

		ComPtr<IDXGISwapChain1> swapChain1;
		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.Width = m_Width;
		desc.Height = m_Height;
		desc.Format = m_Format;
		desc.Stereo = FALSE;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = BufferCount;
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
		m_RTVHandles.clear();
		m_RTVHeap.Reset();

		// Create Descriptor Heap	
		auto device = m_DX12Device->Get();
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = BufferCount;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NodeMask = 0;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		utility::ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_RTVHeap)));
		
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_RTVHeap->GetCPUDescriptorHandleForHeapStart());

		m_BackBuffers.resize(BufferCount);
		m_RTVHandles.resize(BufferCount);
		for (uint32_t i = 0; i < BufferCount; ++i)
		{
			utility::ThrowIfFailed(m_DxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffers[i])));
			device->CreateRenderTargetView(m_BackBuffers[i].Get(), nullptr, rtvHandle);

			m_RTVHandles.push_back(rtvHandle);

#if defined (BUILD_DEBUG)
			utility::SetDebugName(m_BackBuffers[i].Get(), std::format(L"SwapChainBuffer[{:p}]_{}, ", (void*)this, i).c_str());
#endif
			rtvHandle.Offset(1, m_DX12Device->GetRTVDescriptorSize());
		}
	}
}
