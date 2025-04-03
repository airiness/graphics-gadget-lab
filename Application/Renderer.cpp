#include "Precompiled.h"
#include "Renderer.h"
#include "Application.h"

namespace graphicsGadgetLab
{
	Renderer::Renderer() noexcept
	{
		m_Device = std::make_unique<DX12Device>();

		m_DirectCommandQueue = std::make_unique<DX12CommandQueue>(m_Device.get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
		m_ComputeCommandQueue = std::make_unique<DX12CommandQueue>(m_Device.get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
		m_CopyCommandQueue = std::make_unique<DX12CommandQueue>(m_Device.get(), D3D12_COMMAND_LIST_TYPE_DIRECT);

		auto width = Application::Get()->GetWindowWidth();
		auto height = Application::Get()->GetWindowHeight();
		m_SwapChain = std::make_unique<DX12SwapChain>(m_Device.get(), m_DirectCommandQueue.get(), width, height);
	}

	Renderer::~Renderer() noexcept
	{
	}

	void Renderer::Initialize() noexcept
	{
	}

	void Renderer::Update() noexcept
	{
	}

	void Renderer::Finalize() noexcept
	{
	}
}