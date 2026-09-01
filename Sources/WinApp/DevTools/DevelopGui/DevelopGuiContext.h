#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "DevTools/DevelopGui/DevelopGuiStateStore.h"
#include "GGLabRuntime/Graphics/DebugDraw/DebugDraw.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"

namespace gglab
{
	class World;
	class Camera;
	class CameraController;
	class CameraRig;
	class Renderer;
	class AssetManager;
	class EnvironmentAssetController;
	class RenderGraph;
	class DiagnosticsRuntime;
	class DevelopGuiSystem;
	class DebugDrawSystem;
	struct ViewRenderProfile;
	struct ViewRenderSettingsOverrides;
	struct ResolvedTemporalFramePlan;

	class DevelopGuiStateStore;

	struct DevelopGuiContext
	{
		Camera* m_Camera = nullptr;
		CameraController* m_CameraController = nullptr;
		CameraRig* m_CameraRig = nullptr;
		Renderer* m_Renderer = nullptr;
		World* m_World = nullptr;
		std::span<RenderView> m_RenderViews;
		std::span<const RenderQueue> m_RenderQueues;
		RenderView* m_MainRenderView = nullptr;
		AssetManager* m_AssetManager = nullptr;
		EnvironmentAssetController* m_EnvironmentAssetController = nullptr;
		RenderGraph* m_RenderGraph = nullptr;
		DiagnosticsRuntime* m_Diagnostics = nullptr;
		DebugDrawSystem* m_DebugDrawSystem = nullptr;
		DebugDrawFrameView m_DebugDrawFrame{};
		DirectionalShadowSettings* m_DirectionalShadowSettings = nullptr;
		ShadowVisualizationSettings* m_ShadowVisualizationSettings = nullptr;
		const ViewRenderProfile* m_AuthoringViewRenderProfile = nullptr;
		const ViewRenderProfile* m_EffectiveViewRenderProfile = nullptr;
		const ResolvedTemporalFramePlan* m_TemporalFramePlan = nullptr;
		ViewRenderSettingsOverrides* m_ViewRenderSettingsOverrides = nullptr;
		DevelopGuiSystem* m_DevelopGuiSystem = nullptr;

		DevelopGuiStateStore* m_StateStore = nullptr;
		uint64_t m_CurrentPanelKey = 0;

		template <typename T, typename... ARGS> T& PanelState(ARGS&&... args) noexcept
		{
			GGLAB_ASSERT(m_StateStore);
			return m_StateStore->GetOrCreate<T>(m_CurrentPanelKey, std::forward<ARGS>(args)...);
		}

		template <typename T, typename... ARGS> T& StateFor(uint64_t key, ARGS&&... args) noexcept
		{
			GGLAB_ASSERT(m_StateStore);
			return m_StateStore->GetOrCreate<T>(key, std::forward<ARGS>(args)...);
		}
	};
}
