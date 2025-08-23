#pragma once
#include "RendererConstant.h"

namespace gglab
{
	class DX12Device;
	class DX12Texture;
	class DX12Buffer;
	template<typename T>
	class DX12ConstantBuffer;
	class DX12CommandList;
	class DX12Fence;
	class Camera;
	class Cube;
	class RenderPassTexColor;

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

		bool IsInitialized() const noexcept { return m_IsInitialized; }

		DX12Device* GetDevice() const noexcept { return m_Device.get(); }

		DX12RootSignature* GetCommonRootSignature() const noexcept { return m_CommonRootSignature.get(); }

		const DX12Descriptor& GetRenderTarget(RenderTargetIndex rtIndex) const noexcept;
			
		DX12ConstantBuffer<GlobalConstantBuffer>* GetGlobalConstantBuffer() const noexcept { return m_GlobalConstantBuffer.get(); };
	private:
		void InitializeRenderTargets() noexcept;
		void InitializeConstantBuffer() noexcept;
		void InitializeRenderObjects() noexcept;
		void InitializeCamera() noexcept;

		void UpdateGpuBuffers() noexcept;
		void UpdateGlobalConstantBuffer() noexcept;

		void RenderObjects(DX12CommandList* commandList) noexcept;

		void CreateCommonRootSignature() noexcept;

	private:
		std::unique_ptr<DX12Device> m_Device;

		std::unique_ptr<Camera> m_Camera;

		// RenderTargets & DepthStencilBuffer
		RenderTargetArray m_RenderTargets;
		RenderTargetDescriptors m_RenderTargetDescriptors = {};

		// ConstantBuffer
		std::unique_ptr<DX12ConstantBuffer<GlobalConstantBuffer>> m_GlobalConstantBuffer;
		DX12Descriptor m_ConstantBufferDescriptor = {};

		std::unique_ptr<DX12RootSignature> m_CommonRootSignature;

		// TODO: management assets load in property place(maybe SceneLoader?)
		entt::entity m_TestCube = {};
		entt::entity m_TestModel = {};

		std::atomic_bool m_IsInitialized = false;

		std::unique_ptr<RenderPassTexColor> m_TexColorPass;
	};
}
