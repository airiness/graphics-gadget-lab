#pragma once

namespace gglab
{
	struct RenderView;
	class Camera;
	class Renderer;
	class AssetManager;
	class RenderGraph;

	struct DevelopGuiContext
	{
		Camera* m_Camera = nullptr;
		Renderer* m_Renderer = nullptr;
		RenderView* m_MainRenderView = nullptr;
		AssetManager* m_AssetManager = nullptr;
		RenderGraph* m_RenderGraph = nullptr;
	};
}