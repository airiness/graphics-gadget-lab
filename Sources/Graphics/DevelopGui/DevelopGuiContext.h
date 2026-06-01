#pragma once
#include "Graphics/DevelopGui/DevelopGuiStateStore.h"

namespace gglab
{
	struct RenderView;
	class Camera;
	class CameraController;
	class Renderer;
	class AssetManager;
	class RenderGraph;

	class DevelopGuiStateStore;

	struct DevelopGuiContext
	{
		Camera* m_Camera = nullptr;
		CameraController* m_CameraController = nullptr;
		Renderer* m_Renderer = nullptr;
		RenderView* m_MainRenderView = nullptr;
		AssetManager* m_AssetManager = nullptr;
		RenderGraph* m_RenderGraph = nullptr;

		DevelopGuiStateStore* m_StateStore = nullptr;
		uint64_t m_CurrentPanelKey = 0;

		template<typename T, typename... ARGS>
		T& PanelState(ARGS&&... args) noexcept
		{
			GGLAB_ASSERT(m_StateStore);
			return m_StateStore->GetOrCreate<T>(m_CurrentPanelKey, std::forward<ARGS>(args)...);
		}

		template<typename T, typename... ARGS>
		T& StateFor(uint64_t key, ARGS&&... args) noexcept
		{
			GGLAB_ASSERT(m_StateStore);
			return m_StateStore->GetOrCreate<T>(key, std::forward<ARGS>(args)...);
		}
	};
}