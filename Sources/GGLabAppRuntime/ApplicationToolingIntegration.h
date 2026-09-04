#pragma once

#include <cstdint>
#include <span>

namespace gglab
{
	class ApplicationToolingIntegrationBase;
	class AssetManager;
	class CameraRig;
	class DebugDrawSystem;
	class DiagnosticsControl;
	class DiagnosticsView;
	class EnvironmentAssetController;
	class GpuProfilingControlBase;
	class GpuProfilingViewBase;
	class PostProcessPreviewControlBase;
	class PostProcessPreviewViewBase;
	class Renderer;
	class RenderPipelineOverlayExtensionBase;
	class World;
	struct DebugDrawFrameView;
	struct LoadingProgress;
	struct RenderQueue;
	struct RenderView;
	struct ShadowVisualizationSettings;
	struct ViewRenderProfile;

	struct ApplicationToolingInputCapture
	{
		bool m_Keyboard = false;
		bool m_Pointer = false;
	};

	struct ApplicationToolingFrameSettingsResolution
	{
		bool m_GTAOOverrideActive = false;
	};

	struct ApplicationToolingFrameContext
	{
		CameraRig* m_CameraRig = nullptr;
		Renderer* m_Renderer = nullptr;
		World* m_World = nullptr;
		std::span<RenderView> m_RenderViews;
		std::span<const RenderQueue> m_RenderQueues;
		AssetManager* m_AssetManager = nullptr;
		EnvironmentAssetController* m_EnvironmentAssetController = nullptr;
		DiagnosticsView* m_Diagnostics = nullptr;
		DiagnosticsControl* m_DiagnosticsControl = nullptr;
		// Borrowed for Draw only; omitted capabilities remain independently null.
		const GpuProfilingViewBase* m_GpuProfiling = nullptr;
		GpuProfilingControlBase* m_GpuProfilingControl = nullptr;
		const PostProcessPreviewViewBase* m_PostProcessPreview = nullptr;
		PostProcessPreviewControlBase* m_PostProcessPreviewControl = nullptr;
		DebugDrawSystem* m_DebugDrawSystem = nullptr;
		const DebugDrawFrameView* m_DebugDrawFrame = nullptr;
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
		[[nodiscard]] virtual ApplicationToolingFrameSettingsResolution ResolveFrameSettings(
			const ViewRenderProfile& authoringProfile,
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
