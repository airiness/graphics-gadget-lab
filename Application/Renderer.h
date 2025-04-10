#pragma once
#include "RendererConstant.h"

namespace graphicsGadgetLab
{
	class DX12Device;
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
		void Render() noexcept;
		void Finalize() noexcept;

		DX12Device* GetDevice() const noexcept { return m_Device.get(); }

	private:
		void InitializeRootSignatures() noexcept;
		void InitializePipelineStates() noexcept;

	private:
		std::unique_ptr<DX12Device> m_Device;

		// RenderTargets
		// DepthStencil

		RootSignatureArray m_RootSignatures;
		PSOArray m_PipelineStates;

	};
}
