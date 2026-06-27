#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12SwapChain.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"
#include "Core/HResult.h"

namespace gglab
{
	DX12SwapChain::~DX12SwapChain()
	{
		Finalize();
	}

	bool DX12SwapChain::Initialize(const CreateInfo& createInfo) noexcept
	{
		if (IsValid())
		{
			Finalize();
		}

		GGLAB_ASSERT_MSG(createInfo.m_DX12Device != nullptr, "DX12SwapChain::Initialize: device is null.");
		GGLAB_ASSERT_MSG(createInfo.m_PresentQueue != nullptr, "DX12SwapChain::Initialize: present queue is null.");
		GGLAB_ASSERT_MSG(createInfo.m_Hwnd != nullptr, "DX12SwapChain::Initialize: hwnd is null.");
		GGLAB_ASSERT_MSG(createInfo.m_Width > 0 && createInfo.m_Height > 0, "DX12SwapChain::Initialize: invalid size.");
		GGLAB_ASSERT_MSG(createInfo.m_BufferCount >= 2, "DX12SwapChain::Initialize: bufferCount must be >= 2.");

		m_DX12Device = createInfo.m_DX12Device;
		m_PresentQueue = createInfo.m_PresentQueue;
		m_Hwnd = createInfo.m_Hwnd;
		m_Width = createInfo.m_Width;
		m_Height = createInfo.m_Height;
		m_Format = createInfo.m_Format;
		m_BufferCount = createInfo.m_BufferCount;
		m_AllowTearing = createInfo.m_AllowTearing;
		m_Vsync = createInfo.m_Vsync;

		m_DxgiSwapChain = CreateSwapChain();
		if (!m_DxgiSwapChain)
		{
			Reset();
			return false;
		}

		RefreshCurrentBackBufferIndex();
		AcquireBackBuffers();
		CreateSyncObjects();

		return true;
	}

	void DX12SwapChain::Finalize() noexcept
	{
		if (!IsValid())
		{
			Reset();
			return;
		}

		WaitAllSyncObjects();
		Reset();
	}

	void DX12SwapChain::Resize(uint32_t width, uint32_t height) noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "DX12SwapChain::Resize called on invalid swapchain.");

		if (width == 0 || height == 0)
		{
			return;
		}

		if (width == m_Width && height == m_Height)
		{
			return;
		}

		// Wait sync objects finished
		WaitAllSyncObjects();

		m_Width = width;
		m_Height = height;

		// Release backbuffers
		ReleaseBackBuffers();

		DXGI_SWAP_CHAIN_DESC desc = {};
		GGLAB_HR(m_DxgiSwapChain->GetDesc(&desc));

		GGLAB_HR(m_DxgiSwapChain->ResizeBuffers(m_BufferCount, m_Width, m_Height, m_Format, desc.Flags));

		RefreshCurrentBackBufferIndex();
		AcquireBackBuffers();
		CreateSyncObjects();
	}

	void DX12SwapChain::WaitFrameCompletion() noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "DX12SwapChain::WaitFrameCompletion called on invalid swapchain.");
		GGLAB_ASSERT_MSG(m_BackBufferIndex < m_SyncObjects.size(), "BackBufferIndex out of range.");

		auto& fencePoint = m_SyncObjects[m_BackBufferIndex];
		if (fencePoint.IsValid())
		{
			m_DX12Device->WaitForFenceCompletion(fencePoint);
		}
	}

	void DX12SwapChain::SetFrameCompletionFence(const RHIFencePoint& fencePoint) noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "DX12SwapChain::SetFrameCompletionFence called on invalid swapchain.");
		GGLAB_ASSERT_MSG(m_BackBufferIndex < m_SyncObjects.size(), "BackBufferIndex out of range.");

		m_SyncObjects[m_BackBufferIndex] = fencePoint;
	}

	RHIFormat DX12SwapChain::GetFormat() const noexcept
	{
		return ToRHIFormat(m_Format);
	}

	void DX12SwapChain::Present() noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "DX12SwapChain::Present called on invalid swapchain.");

		UINT syncInterval = m_Vsync ? 1u : 0u;
		UINT flags = 0u;
		if (!m_Vsync && m_AllowTearing)
		{
			flags |= DXGI_PRESENT_ALLOW_TEARING;
		}

		GGLAB_HR(m_DxgiSwapChain->Present(syncInterval, flags));

		RefreshCurrentBackBufferIndex();
	}

	DX12Texture* DX12SwapChain::GetBackBuffer(uint32_t bufferIndex) const noexcept
	{
		const RHITextureHandle handle = GetBackBufferHandle(bufferIndex);
		return m_DX12Device ? m_DX12Device->ResolveTexture(handle) : nullptr;
	}

	RHITextureHandle DX12SwapChain::GetBackBufferHandle(uint32_t bufferIndex) const noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "DX12SwapChain::GetBackBufferHandle called on invalid swapchain.");
		GGLAB_ASSERT_MSG(bufferIndex < m_BackBuffers.size(), "BackBuffer index out of range.");

		return m_BackBuffers[bufferIndex];
	}

	ComPtr<IDXGISwapChain4> DX12SwapChain::CreateSwapChain() noexcept
	{
		GGLAB_ASSERT(m_DX12Device && m_PresentQueue && m_Hwnd);
		auto* factory = m_DX12Device->GetDXGIFactory();

		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.Width = m_Width;
		desc.Height = m_Height;
		desc.Format = m_Format;
		desc.Stereo = FALSE;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = m_BufferCount;
		desc.Scaling = DXGI_SCALING_STRETCH;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		desc.Flags = 0;
		if (m_AllowTearing)
		{
			desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		}

		ComPtr<IDXGISwapChain1> swapChain1;
		GGLAB_HR(factory->CreateSwapChainForHwnd(
			m_PresentQueue->Get(),
			m_Hwnd,
			&desc,
			nullptr,
			nullptr,
			&swapChain1));

		// Invalid Alt+Enter Fullscreen
		GGLAB_HR(factory->MakeWindowAssociation(m_Hwnd,
			DXGI_MWA_NO_ALT_ENTER |
			DXGI_MWA_NO_WINDOW_CHANGES));	// TODO: support window changes

		ComPtr<IDXGISwapChain4> swapChain4;
		GGLAB_HR(swapChain1.As(&swapChain4));

		return swapChain4;
	}

	void DX12SwapChain::AcquireBackBuffers() noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "AcquireBackBuffers called on invalid swapchain.");

		m_BackBuffers.clear();
		m_BackBuffers.resize(m_BufferCount);

		for (uint32_t i = 0; i < m_BufferCount; ++i)
		{
			ComPtr<ID3D12Resource> backBuffer;
			GGLAB_HR(m_DxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

			const D3D12_RESOURCE_DESC nativeDesc = backBuffer->GetDesc();
			const std::string debugName = "SwapChain.BackBuffer." + std::to_string(i);

			DX12ResourceManager::ImportedTextureDesc importDesc{};
			importDesc.m_RHI.m_Desc.m_Dimension = RHITextureDimension::Texture2D;
			importDesc.m_RHI.m_Desc.m_Format = ToRHIFormat(nativeDesc.Format);
			importDesc.m_RHI.m_Desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Present;
			importDesc.m_RHI.m_Desc.m_Extent =
			{
				.m_Width = static_cast<uint32_t>(nativeDesc.Width),
				.m_Height = nativeDesc.Height,
				.m_Depth = 1,
			};
			importDesc.m_RHI.m_Desc.m_ArraySize = nativeDesc.DepthOrArraySize;
			importDesc.m_RHI.m_Desc.m_MipLevels = nativeDesc.MipLevels;
			importDesc.m_RHI.m_Desc.m_SampleCount = nativeDesc.SampleDesc.Count;
			importDesc.m_RHI.m_Desc.m_DebugName = debugName.c_str();
			importDesc.m_RHI.m_External.m_InitialState =
			{
				.m_Access = RHIAccess::Present,
				.m_Layout = RHILayout::Present,
			};
			importDesc.m_RHI.m_External.m_DebugName = debugName.c_str();
			importDesc.m_Resource = std::move(backBuffer);

			m_BackBuffers[i] = m_DX12Device->ImportTexture(importDesc);
			GGLAB_ASSERT_MSG(m_BackBuffers[i].IsValid(), "DX12SwapChain failed to import backbuffer.");
		}
	}

	void DX12SwapChain::ReleaseBackBuffers() noexcept
	{
		if (m_DX12Device)
		{
			for (RHITextureHandle& backBuffer : m_BackBuffers)
			{
				if (backBuffer.IsValid())
				{
					m_DX12Device->DestroyTexture(backBuffer);
					backBuffer.Reset();
				}
			}

			// ResizeBuffers requires all old backbuffer references to be released.
			// SwapChain waits for its sync objects before calling this path, so
			// borrowed backbuffers have no remaining GPU use and can retire now.
			m_DX12Device->RetireCompletedRHIResources();
		}
		m_BackBuffers.clear();
	}

	void DX12SwapChain::CreateSyncObjects() noexcept
	{
		m_SyncObjects.clear();
		m_SyncObjects.resize(m_BufferCount);
	}

	void DX12SwapChain::RefreshCurrentBackBufferIndex() noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "RefreshCurrentBackBufferIndex called on invalid swapchain.");
		m_BackBufferIndex = m_DxgiSwapChain->GetCurrentBackBufferIndex();
		GGLAB_ASSERT_MSG(m_BackBufferIndex < m_BufferCount, "DXGI returned invalid back buffer index.");
	}

	void DX12SwapChain::WaitAllSyncObjects() noexcept
	{
		for (auto& fencePoint : m_SyncObjects)
		{
			if (fencePoint.IsValid())
			{
				m_DX12Device->WaitForFenceCompletion(fencePoint);
			}
		}
	}

	void DX12SwapChain::Reset() noexcept
	{
		ReleaseBackBuffers();
		m_SyncObjects.clear();
		m_DxgiSwapChain.Reset();

		m_DX12Device = nullptr;
		m_PresentQueue = nullptr;
		m_Hwnd = nullptr;
		m_Width = 0;
		m_Height = 0;
		m_Format = DXGI_FORMAT_UNKNOWN;
		m_BufferCount = 2;
		m_BackBufferIndex = 0;
		m_AllowTearing = false;
		m_Vsync = true;
	}
}
