#include "Precompiled.h"
#include "RenderPassTexColor.h"
#include "RenderGraph.h"

namespace graphicsGadgetLab
{
	void RenderPassTexColor::AddPass(RenderGraph& rg) noexcept
	{
		struct TexColorData
		{

		};

		const auto& texColorPass = rg.AddPass<TexColorData>("RenderPassTexColor",
			[](RenderGraph::RGBuilder& builder, TexColorData& data)
			{

			},
			[](DX12CommandList* commandList, TexColorData& data)
			{

			});
	}
}


