#pragma once
#include "DevTools/DevelopGui/DevelopGuiRegistry.h"
#include "Graphics/ShadowSettings.h"

namespace gglab
{
	struct DevelopGuiContext;

	struct RenderVisualizationSettings
	{
		ShadowVisualizationSettings m_Shadow;
	};

	class DevToolsRuntime
	{
	public:
		DevToolsRuntime() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevToolsRuntime);
		~DevToolsRuntime() = default;

		void Reset() noexcept;
		void Draw(DevelopGuiContext& context) noexcept;

		DevelopGuiRegistry& GetRegistry() noexcept { return m_Registry; }
		RenderVisualizationSettings& GetRenderVisualizationSettings() noexcept { return m_RenderVisualizationSettings; }

	private:
		DevelopGuiRegistry m_Registry;
		RenderVisualizationSettings m_RenderVisualizationSettings{};
	};
}
