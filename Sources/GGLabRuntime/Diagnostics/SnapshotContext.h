#pragma once

#include "Graphics/RenderView.h"

namespace gglab
{
	class Renderer;
	class AssetManager;
	class EnvironmentAssetController;
	class TaskSystem;
	class World;
	class RenderGraph;
	struct ViewRenderProfile;

	struct SnapshotContext
	{
		Renderer* m_Renderer = nullptr;
		AssetManager* m_AssetManager = nullptr;
		const EnvironmentAssetController* m_EnvironmentAssetController = nullptr;
		const TaskSystem* m_TaskSystem = nullptr;
		World* m_World = nullptr;
		RenderGraph* m_RenderGraph = nullptr;
		std::span<RenderView> m_RenderViews;
		RenderView* m_MainRenderView = nullptr;
		const ViewRenderProfile* m_AuthoringViewRenderProfile = nullptr;
		const ViewRenderProfile* m_EffectiveViewRenderProfile = nullptr;
		bool m_GTAOOverrideActive = false;
	};
}
