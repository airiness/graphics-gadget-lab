#pragma once
#include "RenderPassBase.h"

namespace gglab
{
	class DX12Device;
	class DX12CommandList;
	class DX12PipelineState;
	class RenderGraph;
	class RenderPassTexColor : public RenderPassBase
	{
	public:
		explicit RenderPassTexColor(DX12Device* dx12Device) noexcept;
		~RenderPassTexColor() override = default;
		void AddPass(RenderGraph& rg) noexcept override;

	private:
		//void InitializePSO() noexcept;

		void DrawModels(DX12CommandList* commandList) noexcept;
	private:
		DX12Device* m_DX12Device = nullptr;

		//std::unique_ptr<DX12PipelineState> m_PSO;
	};
}