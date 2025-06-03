#pragma once
#include "RendererConstant.h"

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12Texture;
	class DX12Buffer;
	template<typename T>
	class DX12ConstantBuffer;
	class DX12Fence;

	class Cube;

	class Renderer
	{
	public:
		Renderer() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Renderer);
		~Renderer() noexcept;

		void Initialize() noexcept;
		void Update() noexcept;
		void Render() noexcept;
		void Finalize() noexcept;

		DX12Device* GetDevice() const noexcept { return m_Device.get(); }

	private:
		void InitializeRootSignatures() noexcept;
		void InitializePipelineStates() noexcept;
		void InitializeRenderTargets() noexcept;
		void InitializeConstantBuffer() noexcept;
		void InitializeSyncObjects() noexcept;

		void InitializeRenderObjects() noexcept;

		void UpdateGpuBuffers() noexcept;
		void UpdateGlobalConstantBuffer() noexcept;

	private:
		std::unique_ptr<DX12Device> m_Device;

		// RenderTargets & DepthStencilBuffer
		RenderTargetArray m_RenderTargets;
		RenderTargetDescriptors m_RenderTargetDescriptors;

		// ConstantBuffer
		std::unique_ptr<DX12ConstantBuffer<GlobalConstantBuffer>> m_GlobalConstantBuffer;
		DX12Descriptor m_ConstantBufferDescriptor;

		RootSignatureArray m_RootSignatures;
		PSOArray m_PipelineStates;

		std::unique_ptr<DX12Fence> m_Fence;

		// TODO: temopary declare cube here
		std::unique_ptr<Cube> m_TestCube;
	};
}
