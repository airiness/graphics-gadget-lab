#pragma once
#include "DX12Device.h"
#include "DX12CommandQueue.h"

namespace graphicsGadgetLab
{
	class Renderer
	{
	public:
		explicit Renderer() noexcept;
		~Renderer() noexcept;

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		Renderer(Renderer&&) = delete;
		Renderer& operator=(Renderer&&) = delete;

		void Initialize() noexcept;
		void Update() noexcept;
		void Finalize() noexcept;

		const DX12Device& GetDevice() const noexcept { return m_Device; }
		const DX12CommandQueue& GetDirectCommandQueue() const noexcept { return m_DirectCommandQueue; }
		const DX12CommandQueue& GetComputeCommandQueue() const noexcept { return m_ComputeCommandQueue; }
		const DX12CommandQueue& GetCopyCommandQueue() const noexcept { return m_CopyCommandQueue; }

	private:
		DX12Device m_Device;

		DX12CommandQueue m_DirectCommandQueue;
		DX12CommandQueue m_ComputeCommandQueue;
		DX12CommandQueue m_CopyCommandQueue;

	};
}
