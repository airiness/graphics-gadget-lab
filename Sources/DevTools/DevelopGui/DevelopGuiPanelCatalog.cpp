#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/DevelopGuiPanelCatalog.h"
#include "DevTools/DevelopGui/DevelopGuiRegistry.h"
#include "DevTools/DevelopGui/Panels/CameraInspectorPanel.h"
#include "DevTools/DevelopGui/Panels/IBLViewerPanel.h"
#include "DevTools/DevelopGui/Panels/ImGuiToolsPanel.h"
#include "DevTools/DevelopGui/Panels/RenderGraphInspectorPanel.h"
#include "DevTools/DevelopGui/Panels/ResourceManagementPanel.h"
#include "DevTools/DevelopGui/Panels/ShadowInspectorPanel.h"

namespace gglab::devtools
{
	void RegisterDefaultDevelopGuiPanels(DevelopGuiRegistry& registry) noexcept
	{
		registry.RegisterPanel(std::make_unique<CameraInspectorPanel>());
		registry.RegisterPanel(std::make_unique<ImGuiToolsPanel>());
		registry.RegisterPanel(std::make_unique<IBLViewerPanel>());
		registry.RegisterPanel(std::make_unique<RenderGraphInspectorPanel>());
		registry.RegisterPanel(std::make_unique<ResourceManagementPanel>());
		registry.RegisterPanel(std::make_unique<ShadowInspectorPanel>());
	}
}
