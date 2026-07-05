#pragma once
#include "Application/Demo/DemoBase.h"
#include "Application/Lab/LabRuntime.h"

namespace gglab
{
	class DemoLabHost final : public DemoBase
	{
	public:
		explicit DemoLabHost(const DemoCreateInfo& createInfo) noexcept;
		~DemoLabHost() override = default;

		std::string_view GetName() const noexcept override { return "Demo.LabHost"; }

		void OnEnter() noexcept override;
		void OnResize(uint32_t width, uint32_t height) noexcept override;
		void OnExit() noexcept override;
		void Update() noexcept override;
		void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept override;

		World& GetWorld() noexcept override { return m_Runtime.GetWorld(); }
		Camera& GetCamera() noexcept override { return m_Runtime.GetCamera(); }
		CameraController& GetCameraController() noexcept override
		{
			return m_Runtime.GetCameraController();
		}
		RenderPipelineBase& GetRenderPipeline() noexcept override
		{
			return m_Runtime.GetRenderPipeline();
		}

		LabRuntime& GetLabRuntime() noexcept { return m_Runtime; }
		const LabRuntime& GetLabRuntime() const noexcept { return m_Runtime; }

	private:
		LabRuntime m_Runtime;
	};
}
