#include "Precompiled.h"
#include "DX12SwapChain.h"
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
		m_BufferFormat(bufferFormat),
		m_DxgiSwapChain(CreateSwapChain())
	{
	}

	DX12SwapChain::~DX12SwapChain() noexcept
	{
	}
	ComPtr<IDXGISwapChain4> DX12SwapChain::CreateSwapChain() noexcept
	{
		auto hWnd = Application::Get()->GetHwnd();
		auto factory = m_DX12Device->GetDXGIFactory();

		DXGI_SWAP_CHAIN_DESC1 desc = {};

		factory->CreateSwapChainForHwnd(m_DX12CommandQueue->Get(), hWnd, );

	/*	auto* dxgiFactory = */


		return ComPtr<IDXGISwapChain4>();
	}
}
