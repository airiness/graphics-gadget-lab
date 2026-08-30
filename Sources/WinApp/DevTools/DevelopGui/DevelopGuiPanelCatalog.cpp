#include "DevTools/DevelopGui/DevelopGuiPanelCatalog.h"
#include "DevTools/DevelopGui/DevelopGuiRegistry.h"
#include "DevTools/DevelopGui/Panels/AssetManagerPanel.h"
#include "DevTools/DevelopGui/Panels/CameraInspectorPanel.h"
#include "DevTools/DevelopGui/Panels/DebugDrawPanel.h"
#include "DevTools/DevelopGui/Panels/DX12BackendSummaryPanel.h"
#include "DevTools/DevelopGui/Panels/EntityPanel.h"
#include "DevTools/DevelopGui/Panels/ForwardPlusInspectorPanel.h"
#include "DevTools/DevelopGui/Panels/GTAOInspectorPanel.h"
#include "DevTools/DevelopGui/Panels/IBLViewerPanel.h"
#include "DevTools/DevelopGui/Panels/ImGuiToolsPanel.h"
#include "DevTools/DevelopGui/Panels/RenderGraphInspectorPanel.h"
#include "DevTools/DevelopGui/Panels/RenderViewPanel.h"
#include "DevTools/DevelopGui/Panels/ResourceManagementPanel.h"
#include "DevTools/DevelopGui/Panels/ShadowInspectorPanel.h"
#include "DevTools/DevelopGui/Panels/TransientResourcePoolPanel.h"
#include "DevTools/DevelopGui/Panels/PersistentSceneBuffersPanel.h"
#include "DevTools/DevelopGui/Panels/PipelineSystemPanel.h"
#include "DevTools/DevelopGui/Panels/PostProcessInspectorPanel.h"
#include "DevTools/DevelopGui/Panels/ProfilingPanel.h"
#include "DevTools/DevelopGui/Panels/TaskSystemPanel.h"
#include "DevTools/DevelopGui/Panels/TemporalAAInspectorPanel.h"
#include "Graphics/RHI/RHIContext.h"
#include "Graphics/RHI/DX12/DX12Context.h"
#if GGLAB_ENABLE_VULKAN
#include "DevTools/DevelopGui/Panels/VulkanBackendSummaryPanel.h"
#include "Graphics/RHI/Vulkan/VulkanContext.h"
#endif

namespace gglab::devtools
{
	void RegisterDefaultDevelopGuiPanels(
		DevelopGuiRegistry& registry, RHIContext& rhiContext) noexcept
	{
		registry.RegisterPanel(std::make_unique<AssetManagerPanel>());
		registry.RegisterPanel(std::make_unique<CameraInspectorPanel>());
		registry.RegisterPanel(std::make_unique<DebugDrawPanel>());
		registry.RegisterPanel(std::make_unique<EntityPanel>());
		registry.RegisterPanel(std::make_unique<ForwardPlusInspectorPanel>());
		registry.RegisterPanel(std::make_unique<GTAOInspectorPanel>());
		registry.RegisterPanel(std::make_unique<ImGuiToolsPanel>());
		registry.RegisterPanel(std::make_unique<IBLViewerPanel>());
		registry.RegisterPanel(std::make_unique<RenderGraphInspectorPanel>());
		registry.RegisterPanel(std::make_unique<RenderViewPanel>());
		registry.RegisterPanel(std::make_unique<ResourceManagementPanel>());
		registry.RegisterPanel(std::make_unique<ShadowInspectorPanel>());
		registry.RegisterPanel(std::make_unique<TransientResourcePoolPanel>());
		registry.RegisterPanel(std::make_unique<PersistentSceneBuffersPanel>());
		registry.RegisterPanel(std::make_unique<PipelineSystemPanel>());
		registry.RegisterPanel(std::make_unique<PostProcessInspectorPanel>());
		registry.RegisterPanel(std::make_unique<ProfilingPanel>());
		registry.RegisterPanel(std::make_unique<TaskSystemPanel>());
		registry.RegisterPanel(std::make_unique<TemporalAAInspectorPanel>());
		if (dynamic_cast<DX12Context*>(&rhiContext))
		{
			registry.RegisterPanel(std::make_unique<DX12BackendSummaryPanel>());
		}
#if GGLAB_ENABLE_VULKAN
		else if (dynamic_cast<VulkanContext*>(&rhiContext))
		{
			registry.RegisterPanel(std::make_unique<VulkanBackendSummaryPanel>());
		}
#endif
	}
}
