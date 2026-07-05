#pragma once
#include "Application/Demo/DemoTypes.h"
#include "Application/Lab/LabParameter.h"
#include "Application/Lab/LabTypes.h"
#include "Core/World.h"

namespace gglab
{
	class Camera;
	class CameraController;
	class RenderPipelineBase;

	struct LabSessionCreateInfo
	{
		DemoServices m_Services{};
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;

		bool IsValid() const noexcept
		{
			return m_Services.IsValid() && m_WindowWidth > 0 && m_WindowHeight > 0;
		}
	};

	class LabSession
	{
	public:
		virtual ~LabSession();
		GGLAB_DELETE_COPYABLE_MOVABLE(LabSession);

		const LabDescriptor& GetDescriptor() const noexcept { return m_Descriptor; }
		const LabParameterSet& GetParameters() const noexcept { return m_Parameters; }
		bool IsValid() const noexcept;
		bool SetParameter(
			const LabParameterId& id,
			const LabValue& value,
			LabChangeImpact* impact = nullptr) noexcept;
		LabChangeImpact ResetParameters() noexcept;
		void ApplyParameterChanges(LabChangeImpact impact) noexcept;

		virtual void OnEnter() noexcept {}
		virtual void OnExit() noexcept {}
		virtual void Update() noexcept = 0;
		virtual void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept
		{
			GGLAB_UNUSED(feedback);
		}

		virtual void OnResize(uint32_t width, uint32_t height) noexcept;

		World& GetWorld() noexcept { return m_World; }
		Camera& GetCamera() noexcept { return *m_Camera; }
		CameraController& GetCameraController() noexcept { return *m_CameraController; }
		RenderPipelineBase& GetRenderPipeline() noexcept { return *m_RenderPipeline; }

	protected:
		LabSession(
			LabDescriptor descriptor,
			const LabSessionCreateInfo& createInfo,
			std::unique_ptr<RenderPipelineBase> renderPipeline) noexcept;

		void UpdateCamera() noexcept;
		LabParameterSet& GetMutableParameters() noexcept { return m_Parameters; }

		virtual void ApplyImmediateParameters() noexcept {}
		virtual void RebuildScene() noexcept {}
		virtual void RecreatePipeline() noexcept {}

		void SetRenderPipeline(std::unique_ptr<RenderPipelineBase> renderPipeline) noexcept;

		DemoServices m_Services{};
		World m_World;

	private:
		LabDescriptor m_Descriptor;
		LabParameterSet m_Parameters;
		std::unique_ptr<Camera> m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		std::unique_ptr<RenderPipelineBase> m_RenderPipeline;
	};
}
