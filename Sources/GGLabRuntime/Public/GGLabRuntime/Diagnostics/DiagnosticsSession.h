#pragma once

#include "GGLabRuntime/Diagnostics/DiagnosticsControl.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"
#include "GGLabRuntime/Graphics/RenderView.h"

#include <memory>
#include <span>

namespace gglab
{
	class AssetManager;
	class EnvironmentAssetController;
	class LabSnapshotSourceBase;
	class Renderer;
	class RenderGraph;
	class TaskSystem;
	class World;
	struct ResolvedTemporalFramePlan;
	struct ViewRenderProfile;

	// Borrowed live frame inputs for one Runtime-owned diagnostics capture
	// interval. The session captures requested providers while these inputs
	// remain valid; published snapshots own no part of them.
	struct DiagnosticsFrameContext
	{
		Renderer* m_Renderer = nullptr;
		AssetManager* m_AssetManager = nullptr;
		const EnvironmentAssetController* m_EnvironmentAssetController = nullptr;
		const LabSnapshotSourceBase* m_LabSnapshotSource = nullptr;
		const TaskSystem* m_TaskSystem = nullptr;
		World* m_World = nullptr;
		RenderGraph* m_RenderGraph = nullptr;
		std::span<RenderView> m_RenderViews;
		std::span<const RenderQueue> m_RenderQueues;
		RenderView* m_MainRenderView = nullptr;
		const ViewRenderProfile* m_AuthoringViewRenderProfile = nullptr;
		const ViewRenderProfile* m_EffectiveViewRenderProfile = nullptr;
		const ResolvedTemporalFramePlan* m_TemporalFramePlan = nullptr;
		bool m_GTAOOverrideActive = false;
	};

	struct DiagnosticsSessionCreateInfo
	{
		// Registers the provider that publishes the current Lab snapshot source.
		// The source itself remains optional on every frame context.
		bool m_RegisterLabSnapshotProvider = false;
	};

	// Runtime-owned diagnostics session. The capture engine, provider registry,
	// snapshot store and borrowed live capture context stay Runtime-private.
	// Hosts own only this optional session lifetime and drive paired begin/end
	// frames around tooling; published snapshots remain immutable.
	class DiagnosticsSession
	{
	public:
		virtual ~DiagnosticsSession() = default;

		[[nodiscard]] virtual DiagnosticsView* GetView() noexcept = 0;
		[[nodiscard]] virtual DiagnosticsControl* GetControl() noexcept = 0;
		virtual void BeginFrame(const DiagnosticsFrameContext& context) noexcept = 0;
		virtual void EndFrame() noexcept = 0;
	};

	// Creates the diagnostics engine, the built-in Runtime providers and the
	// active backend provider set. Returns null when no initialized RHI
	// context is available.
	[[nodiscard]] std::unique_ptr<DiagnosticsSession> CreateDiagnosticsSession(
		Renderer& renderer, DiagnosticsSessionCreateInfo createInfo = {}) noexcept;
}
