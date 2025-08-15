#include "Precompiled.h"
#include "RenderGraph.h"
#include "RGPass.h"
#include "DX12Device.h"
#include "DX12Texture.h"
#include "DX12Buffer.h"
#include "DX12SwapChain.h"

namespace graphicsGadgetLab
{
	RenderGraph::RenderGraph(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device),
		m_GpuResourceRegistry(dx12Device),
		m_ArenaAllocator(1u << 20)
	{
	}

	RenderGraph::~RenderGraph() noexcept
	{
	}

	void RenderGraph::Compile() noexcept
	{
		// TODO: DAG
	}

	void RenderGraph::Execute() noexcept
	{
		auto* swapChain = m_DX12Device->GetSwapChain();
		const auto currentBufferIndex = swapChain->GetCurrentBackBufferIndex();
		auto* graphicsCommandList = m_DX12Device->GetGraphicsCommandList(currentBufferIndex);

		for (auto& passNode : m_PassNodes)
		{
			passNode.m_Pass->Execute(graphicsCommandList);
		}

	}
}
