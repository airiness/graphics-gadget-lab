#pragma once
#include "Application/Demo/DemoTypes.h"
#include "Application/LoadingProgress.h"
#include "Application/Lab/LabParameter.h"
#include "Application/Lab/LabRunConfig.h"
#include "Application/Lab/LabTypes.h"
#include "Core/World.h"
#include "Graphics/CameraRig.h"
#include "Graphics/PostProcess/ViewRenderSettings.h"

namespace gglab
{
	class Camera;
	class CameraController;
	class AssetOwnerScope;
	class RenderPipelineBase;
	struct LabDiagnosticsSnapshot;

	struct LabSessionCreateInfo
	{
		DemoServices m_Services{};
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;
		LabRunConfig m_RunConfig{};

		bool IsValid() const noexcept
		{
			return m_Services.IsValid() && m_WindowWidth > 0 && m_WindowHeight > 0;
		}
	};

	class LabSessionBase
	{
	public:
		virtual ~LabSessionBase();
		GGLAB_DELETE_COPYABLE_MOVABLE(LabSessionBase);

		const LabDescriptor& GetDescriptor() const noexcept { return m_Descriptor; }
		const LabParameterSet& GetParameters() const noexcept { return m_Parameters; }
		bool IsValid() const noexcept;
		bool SetParameter(const LabParameterId& id, const LabValue& value,
			LabChangeImpact* impact = nullptr) noexcept;
		LabChangeImpact ResetParameters() noexcept;
		void ApplyParameterChanges(LabChangeImpact impact) noexcept;
		void ApplyRestoredParametersForPrepare(LabChangeImpact impact) noexcept;

		virtual void BeginPrepare() noexcept {}
		virtual void TickPrepare() noexcept {}
		virtual LoadingProgress GetPreparationProgress() const noexcept
		{
			return LoadingProgress::Ready();
		}
		virtual void CommitPrepare() noexcept {}
		virtual void CancelPrepare() noexcept {}

		virtual void OnEnter() noexcept {}
		virtual void OnExit() noexcept {}
		virtual void Update(float deltaTime) noexcept = 0;
		virtual void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept
		{
			GGLAB_UNUSED(feedback);
		}
		virtual void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept
		{
			GGLAB_UNUSED(diagnostics);
		}

		virtual void OnResize(uint32_t width, uint32_t height) noexcept;

		World& GetWorld() noexcept { return m_World; }
		const World& GetWorld() const noexcept { return m_World; }
		Camera& GetCamera() noexcept { return *m_Camera; }
		const Camera& GetCamera() const noexcept { return *m_Camera; }
		CameraController& GetCameraController() noexcept { return *m_CameraController; }
		CameraRig& GetCameraRig() noexcept { return m_CameraRig; }
		const ViewRenderProfile& GetViewRenderProfile() const noexcept
		{
			return m_ViewRenderProfile;
		}
		RenderPipelineBase& GetRenderPipeline() noexcept { return *m_RenderPipeline; }

	protected:
		LabSessionBase(LabDescriptor descriptor, const LabSessionCreateInfo& createInfo,
			std::unique_ptr<RenderPipelineBase> renderPipeline) noexcept;

		void UpdateCamera(float deltaTime) noexcept;
		LabParameterSet& GetMutableParameters() noexcept { return m_Parameters; }
		ViewRenderProfile& GetMutableViewRenderProfile() noexcept { return m_ViewRenderProfile; }
		const LabRunConfig& GetRunConfig() const noexcept { return m_RunConfig; }
		AssetOwnerScope& GetAssetOwnerScope() noexcept;
		void ResetAssetInterests() noexcept;

		virtual void ApplyImmediateParameters() noexcept {}
		virtual void RebuildScene() noexcept {}
		virtual void RecreatePipeline() noexcept {}
		virtual void OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept;

		void SetRenderPipeline(std::unique_ptr<RenderPipelineBase> renderPipeline) noexcept;

		DemoServices m_Services{};
		LabRunConfig m_RunConfig{};
		World m_World;

	private:
		LabDescriptor m_Descriptor;
		std::unique_ptr<AssetOwnerScope> m_AssetOwnerScope;
		LabParameterSet m_Parameters;
		std::unique_ptr<Camera> m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		CameraRig m_CameraRig;
		ViewRenderProfile m_ViewRenderProfile{};
		std::unique_ptr<RenderPipelineBase> m_RenderPipeline;
	};
}
