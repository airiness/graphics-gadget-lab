#pragma once
#include "RenderPassBase.h"

namespace gglab
{
	class DX12CommandList;
	class RenderGraph;
	class RenderPassTexColor : public RenderPassBase
	{
	public:
		void AddPass(RenderGraph& rg) noexcept override;

	private:
		void DrawModels(DX12CommandList* commandList) noexcept;
	};
}