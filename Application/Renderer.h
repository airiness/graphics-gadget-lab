#pragma once
#include "DX12Device.h"
#include "DX12CommandQueue.h"

namespace graphicsGadgetLab
{
	class Renderer
	{
	public:
		Renderer() noexcept;
		~Renderer() noexcept;

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		Renderer(Renderer&&) = delete;
		Renderer& operator=(Renderer&&) = delete;

		void Initialize() noexcept;
		void Update() noexcept;
		void Finalize() noexcept;

		DX12Device* GetDevice() const noexcept { return m_Device.get(); }
		DX12CommandQueue* GetDirectCommandQueue() const noexcept { return m_DirectCommandQueue.get(); }
		DX12CommandQueue* GetComputeCommandQueue() const noexcept { return m_ComputeCommandQueue.get(); }
		DX12CommandQueue* GetCopyCommandQueue() const noexcept { return m_CopyCommandQueue.get(); }

	private:
		std::unique_ptr<DX12Device> m_Device;

		std::unique_ptr<DX12CommandQueue> m_DirectCommandQueue;
		std::unique_ptr<DX12CommandQueue> m_ComputeCommandQueue;
		std::unique_ptr<DX12CommandQueue> m_CopyCommandQueue;

	};
}
