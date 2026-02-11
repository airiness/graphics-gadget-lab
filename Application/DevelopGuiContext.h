#pragma once

namespace gglab
{
	class Camera;
	class Renderer;
	class AssetManager;
	class RenderView;
	class RenderGraph;

	struct DevelopGuiContext
	{
		Camera& m_Camera;
		Renderer* m_Renderer = nullptr;
		RenderView* m_MainRenderView = nullptr;
		AssetManager* m_AssetManager = nullptr;
		RenderGraph* m_RenderGraph = nullptr;
	};
}