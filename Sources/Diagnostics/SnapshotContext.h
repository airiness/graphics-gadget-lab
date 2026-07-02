#pragma once

#include "Graphics/RenderView.h"

namespace gglab
{
	class Renderer;
	class AssetManager;
	class World;
	class RenderGraph;

	struct SnapshotContext
	{
		Renderer* m_Renderer = nullptr;
		AssetManager* m_AssetManager = nullptr;
		World* m_World = nullptr;
		RenderGraph* m_RenderGraph = nullptr;
		std::span<RenderView> m_RenderViews;
		RenderView* m_MainRenderView = nullptr;
	};
}
