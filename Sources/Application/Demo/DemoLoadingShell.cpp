#include "Core/Precompiled.h"
#include "Application/Demo/DemoLoadingShell.h"
#include "Graphics/Camera.h"
#include "Graphics/CameraController.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

namespace gglab
{
	DemoLoadingShell::DemoLoadingShell(const DemoCreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT_MSG(createInfo.IsValid(), "DemoLoadingShell requires valid create info.");

		Camera::CreateInfo cameraCreateInfo{};
		cameraCreateInfo.m_Position = Vector3(0.0f, 0.0f, -5.0f);
		cameraCreateInfo.m_Width = createInfo.m_WindowWidth;
		cameraCreateInfo.m_Height = createInfo.m_WindowHeight;
		cameraCreateInfo.m_Near = 0.1f;
		cameraCreateInfo.m_Far = 1000.0f;
		cameraCreateInfo.m_Fov = 60.0f;
		m_Camera = std::make_unique<Camera>(cameraCreateInfo);

		m_CameraController = std::make_unique<CameraController>(CameraController::CreateInfo{});
		m_CameraRig.AttachMainCamera(*m_Camera, *m_CameraController);
		m_RenderPipeline = std::make_unique<RenderPipelineForwardPBR>();
	}

	void DemoLoadingShell::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_CameraRig.OnResize(width, height);
	}

	void DemoLoadingShell::Update() noexcept
	{
		m_CameraRig.GetActiveCamera().Update();
	}
}
