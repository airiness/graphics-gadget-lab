#include "Application/Lab/LabSessionBase.h"
#include "Core/Input/InputManager.h"
#include "Core/Input/Keyboard.h"
#include "Core/Input/Mouse.h"
#include "Graphics/Camera.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/CameraController.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"

namespace gglab
{
	LabSessionBase::LabSessionBase(LabDescriptor descriptor, const LabSessionCreateInfo& createInfo,
		std::unique_ptr<RenderPipelineBase> renderPipeline) noexcept :
		m_Services(createInfo.m_Services), m_RunConfig(createInfo.m_RunConfig),
		m_Descriptor(std::move(descriptor)), m_RenderPipeline(std::move(renderPipeline))
	{
		GGLAB_ASSERT_MSG(createInfo.IsValid(), "LabSessionBase requires valid create info.");

		Camera::CreateInfo cameraCreateInfo{};
		cameraCreateInfo.m_Position = Vector3(0.0f, 1.0f, -5.0f);
		cameraCreateInfo.m_Width = createInfo.m_WindowWidth;
		cameraCreateInfo.m_Height = createInfo.m_WindowHeight;
		cameraCreateInfo.m_Near = 0.1f;
		cameraCreateInfo.m_Far = 1000.0f;
		cameraCreateInfo.m_Fov = 60.0f;
		m_Camera = std::make_unique<Camera>(cameraCreateInfo);

		CameraController::CreateInfo controllerCreateInfo{};
		controllerCreateInfo.m_Params.m_MovementSpeed = 10.0f;
		controllerCreateInfo.m_Params.m_MouseSensitivityRadPerCount = 0.0018f;
		controllerCreateInfo.m_Params.m_AccelerateMultiplier = 3.0f;
		controllerCreateInfo.m_Params.m_SmoothStepT = 0.5f;
		m_CameraController = std::make_unique<CameraController>(controllerCreateInfo);
		m_CameraRig.AttachMainCamera(*m_Camera, *m_CameraController);
		m_AssetOwnerScope =
			std::make_unique<AssetOwnerScope>(m_Services.m_AssetManager->CreateOwnerScope());
	}

	LabSessionBase::~LabSessionBase() = default;

	bool LabSessionBase::IsValid() const noexcept
	{
		return m_Descriptor.m_Id.IsValid() && m_Services.IsValid() && m_Camera &&
			m_CameraController && m_RenderPipeline;
	}

	bool LabSessionBase::SetParameter(
		const LabParameterId& id, const LabValue& value, LabChangeImpact* impact) noexcept
	{
		return m_Parameters.Set(id, value, impact);
	}

	LabChangeImpact LabSessionBase::ResetParameters() noexcept
	{
		return m_Parameters.ResetAll();
	}

	void LabSessionBase::ApplyParameterChanges(LabChangeImpact impact) noexcept
	{
		ApplyImmediateParameters();
		switch (impact)
		{
		case LabChangeImpact::Immediate:
			break;
		case LabChangeImpact::RebuildScene:
			RebuildScene();
			break;
		case LabChangeImpact::RecreatePipeline:
			RecreatePipeline();
			break;
		case LabChangeImpact::RestartSession:
			RecreatePipeline();
			RebuildScene();
			break;
		}
	}

	void LabSessionBase::ApplyRestoredParametersForPrepare(LabChangeImpact impact) noexcept
	{
		OnParametersRestoredForPrepare(impact);
	}

	void LabSessionBase::OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept
	{
		ApplyParameterChanges(impact);
	}

	void LabSessionBase::OnResize(uint32_t width, uint32_t height) noexcept
	{
		if (width > 0 && height > 0)
		{
			m_CameraRig.OnResize(width, height);
		}
	}

	void LabSessionBase::UpdateCamera(float deltaTime) noexcept
	{
		auto* inputManager = m_Services.m_InputManager;
		GGLAB_ASSERT_NOT_NULL(inputManager);

		auto* mouse = inputManager->GetMouse();
		auto* keyboard = inputManager->GetKeyboard();
		const auto mouseCoord = mouse->GetMouseCoord();

		CameraInput input{};
		input.m_Front = keyboard->IsKeyHeld(KeyCode::W);
		input.m_Back = keyboard->IsKeyHeld(KeyCode::S);
		input.m_Left = keyboard->IsKeyHeld(KeyCode::A);
		input.m_Right = keyboard->IsKeyHeld(KeyCode::D);
		input.m_Up = keyboard->IsKeyHeld(KeyCode::Q);
		input.m_Down = keyboard->IsKeyHeld(KeyCode::E);
		input.m_Accelerate = keyboard->IsKeyHeld(KeyCode::LeftShift);
		input.m_IsMouseRelative = mouse->GetMouseMode() == Mouse::MouseMode::Relative;
		input.m_MouseDelta = mouseCoord;

		Camera& camera = m_CameraRig.GetActiveCamera();
		CameraController& controller = m_CameraRig.GetActiveCameraController();
		controller.Update(camera, input, deltaTime);
		camera.Update();
	}

	AssetOwnerScope& LabSessionBase::GetAssetOwnerScope() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_AssetOwnerScope.get());
		return *m_AssetOwnerScope;
	}

	void LabSessionBase::ResetAssetInterests() noexcept
	{
		if (m_AssetOwnerScope)
		{
			m_AssetOwnerScope->Reset();
		}
	}

	void LabSessionBase::SetRenderPipeline(
		std::unique_ptr<RenderPipelineBase> renderPipeline) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(renderPipeline.get());
		m_RenderPipeline = std::move(renderPipeline);
	}
}
