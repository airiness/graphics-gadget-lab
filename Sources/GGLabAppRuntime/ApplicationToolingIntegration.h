#pragma once

#include <cstdint>
#include <span>

namespace gglab
{
	class ApplicationToolingIntegrationBase;
	class AssetManager;
	class Camera;
	class CameraController;
	class CameraRig;
	class DebugDrawSystem;
	class EnvironmentAssetController;
	class RenderGraph;
	class Renderer;
	class RenderPipelineOverlayExtensionBase;
	class World;
	struct DebugDrawFrameView;
	struct DirectionalShadowSettings;
	struct LoadingProgress;
	struct RenderQueue;
	struct RenderView;
	struct ShadowVisualizationSettings;
	struct ViewRenderProfile;
	struct ResolvedTemporalFramePlan;

	struct ApplicationToolingInputCapture
	{
		bool m_Keyboard = false;
		bool m_Pointer = false;
	};

	struct ApplicationToolingFrameContext
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
		DebugDrawSystem* m_DebugDrawSystem = nullptr;
		const DebugDrawFrameView* m_DebugDrawFrame = nullptr;
		DirectionalShadowSettings* m_DirectionalShadowSettings = nullptr;
		const ViewRenderProfile* m_AuthoringViewRenderProfile = nullptr;
		const ViewRenderProfile* m_EffectiveViewRenderProfile = nullptr;
		const ResolvedTemporalFramePlan* m_TemporalFramePlan = nullptr;
		const LoadingProgress* m_LoadingProgress = nullptr;
	};

	enum class ApplicationToolingFrameEndReason : uint8_t
	{
		Completed,
		Aborted,
	};

	class ApplicationToolingIntegrationBase
	{
	public:
		virtual ~ApplicationToolingIntegrationBase();

		// Release tooling resources that borrow runtime/GPU services. The runtime
		// calls this after GPU quiescence and before destroying those services;
		// the host retains ownership of the integration object.
		virtual void PrepareForShutdown() noexcept = 0;
		[[nodiscard]] virtual ApplicationToolingInputCapture GetPreviousFrameInputCapture()
			const noexcept = 0;
		virtual void ResolveFrameSettings(const ViewRenderProfile& authoringProfile,
			ShadowVisualizationSettings& outShadowVisualizationSettings,
			ViewRenderProfile& outEffectiveProfile) const noexcept = 0;
		[[nodiscard]] virtual bool BeginFrame() noexcept = 0;
		virtual void Draw(const ApplicationToolingFrameContext& context) noexcept = 0;
		virtual void EndFrame(ApplicationToolingFrameEndReason reason) noexcept = 0;
		[[nodiscard]] virtual RenderPipelineOverlayExtensionBase* GetOverlayExtension()
			noexcept = 0;
	};

	// One optional tooling transaction. A successful BeginFrame is paired with
	// exactly one Completed or Aborted callback, including early-return paths.
	class ApplicationToolingFrame final
	{
	public:
		explicit ApplicationToolingFrame(
			ApplicationToolingIntegrationBase* integration) noexcept;
		ApplicationToolingFrame(const ApplicationToolingFrame&) = delete;
		ApplicationToolingFrame& operator=(const ApplicationToolingFrame&) = delete;
		ApplicationToolingFrame(ApplicationToolingFrame&&) = delete;
		ApplicationToolingFrame& operator=(ApplicationToolingFrame&&) = delete;
		~ApplicationToolingFrame() noexcept;

		[[nodiscard]] bool IsOpen() const noexcept { return m_Integration != nullptr; }
		[[nodiscard]] RenderPipelineOverlayExtensionBase* GetOverlayExtension() const noexcept;
		void Draw(const ApplicationToolingFrameContext& context) noexcept;
		void Complete() noexcept;
		void Abort() noexcept;

	private:
		void Close(ApplicationToolingFrameEndReason reason) noexcept;

		ApplicationToolingIntegrationBase* m_Integration = nullptr;
	};
}
