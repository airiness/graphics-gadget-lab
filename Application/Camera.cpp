#include "Precompiled.h"
#include "Camera.h"
#include "Application.h"
#include "Mouse.h"
#include "Keyboard.h"
#include "Time.h"

namespace graphicsGadgetLab
{
	Camera::Camera(const Info& info) noexcept :
		m_Near(info.m_Near),
		m_Far(info.m_Far),
		m_Aspect(static_cast<float>(info.m_Width) / info.m_Height),
		m_Fov(info.m_Fov)
	{
		auto yaw = std::atan2(info.m_Forward.x, info.m_Forward.z);
		auto pitch = std::asin(std::clamp(info.m_Forward.y, -0.98f, 0.98f));

		RotateCamera(yaw, pitch);
	}

	void Camera::Update() noexcept
	{
		const auto app = Application::GetInstance();
		const auto time = app->GetTime();
		const auto dt = time->GetDeltaTime();

		const auto mouse = app->GetMouse();
		const auto keyboard = app->GetKeyboard();

		if (mouse->GetMouseMode() == Mouse::MouseMode::Relative)
		{
			auto mouseCoord = mouse->GetMouseCoord();

			RotateCamera(mouseCoord.x * dt * m_RotationSpeed, mouseCoord.y * dt * m_RotationSpeed);
		}

		Vector3 movement = {};
		if (keyboard->IsKeyHeld(KeyCode::W))
		{
			movement.z += 1.0f;
		}

		if (keyboard->IsKeyHeld(KeyCode::S))
		{
			movement.z -= 1.0f;
		}

		if (keyboard->IsKeyHeld(KeyCode::D))
		{
			movement.x += 1.0f;
		}

		if (keyboard->IsKeyHeld(KeyCode::A))
		{
			movement.x -= 1.0f;
		}

		if (keyboard->IsKeyHeld(KeyCode::Q))
		{
			movement.y += 1.0f;
		}

		if (keyboard->IsKeyHeld(KeyCode::E))
		{
			movement.y -= 1.0f;
		}

		movement = Vector3::Transform(movement, m_Oritation);
		m_Velocity = Vector3::SmoothStep(m_Velocity, movement, SmoothStepT);

		if (m_Velocity.LengthSquared() > std::numeric_limits<float>::epsilon())
		{
			float speedFactor = 1.0f;
			if (keyboard->IsKeyHeld(KeyCode::LeftShift))
			{
				speedFactor *= 3.0f;
			}

			m_Position += m_Velocity * dt * m_MovementSpeed * speedFactor;
		}

		Matrix viewInverse = Matrix::CreateFromQuaternion(m_Oritation) * Matrix::CreateTranslation(m_Position);
		viewInverse.Invert(m_ViewMatrix);
		SetProjMatrix(m_Fov, m_Aspect, m_Near, m_Far);
	}

	void Camera::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_Aspect = static_cast<float>(width) / height;

		m_ProjMatrix = DirectX::XMMatrixPerspectiveFovLH(
			DirectX::XMConvertToRadians(m_Fov), m_Aspect, m_Near, m_Far);
	}

	Matrix Camera::GetViewMatrix() const noexcept
	{
		return m_ViewMatrix;
	}

	Matrix Camera::GetProjMatrix() const noexcept
	{
		return m_ProjMatrix;
	}

	Vector3 Camera::GetDirection() const noexcept
	{
		return Vector3::UnitZ * m_Oritation;
	}

	void Camera::RotateCamera(float yaw, float pitch)
	{
		Quaternion pitchQua = Quaternion::CreateFromYawPitchRoll(0.0f, pitch, 0.0f);
		Quaternion yawQua = Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);

		m_Oritation = pitchQua * m_Oritation * yawQua;
	}

	void Camera::SetProjMatrix(float fov, float aspect, float nearZ, float farZ) noexcept
	{
		m_ProjMatrix = DirectX::XMMatrixPerspectiveFovLH(
			DirectX::XMConvertToRadians(fov), aspect, nearZ, farZ);
	}
}
