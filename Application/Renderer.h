#pragma once

namespace graphicsGadgetLab
{
	enum class RootSignatureIndex : uint32_t
	{
		CommonRootSignature = 0,
		RootSignatureCount
	};

	enum class PSOIndex : uint32_t
	{
		NormalTexturePSO = 0,
		PSOCount
	};

	class DX12Device;
	class DX12RootSignature;
	class DX12PipelinseState;
	class Renderer
	{
	private:
		using RootSignatureArray = std::array<std::unique_ptr<DX12RootSignature>, static_cast<size_t>(RootSignatureIndex::RootSignatureCount)>;
		using PSOArray = std::array<std::unique_ptr<DX12PipelinseState>, static_cast<size_t>(PSOIndex::PSOCount)>;
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
