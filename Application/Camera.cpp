#include "Precompiled.h"
#include "Camera.h"
#include "Application.h"
#include "Mouse.h"
#include "Keyboard.h"
#include "Time.h"

namespace gglab
{
	Camera::Camera(const Info& info) noexcept :
		m_Forward(info.m_Forward),
		m_Up(info.m_Up),
		m_Right(info.m_Right),
		m_Position(info.m_Position),
		m_Near(info.m_Near),
		m_Far(info.m_Far),
		m_Aspect(static_cast<float>(info.m_Width) / info.m_Height),
		m_Fov(info.m_Fov)
	{
		UpdateViewMatrix();
		UpdateProjMatrix();
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

			auto yawDelta = static_cast<float>(dt * mouseCoord.x * m_RotationSpeed);
			auto pitchDelta = static_cast<float>(dt * -mouseCoord.y * m_RotationSpeed);

			m_Yaw += yawDelta;
			m_Pitch += pitchDelta;
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

		m_Velocity = Vector3::SmoothStep(m_Velocity, movement, SmoothStepT);

		if (m_Velocity.LengthSquared() > std::numeric_limits<float>::epsilon())
		{
			float speedFactor = 1.0f;
			if (keyboard->IsKeyHeld(KeyCode::LeftShift))
			{
				speedFactor *= 3.0f;
			}

			auto v = m_Velocity * static_cast<float>(dt) * m_MovementSpeed * speedFactor;

			m_Position += v.x * m_Right;
			m_Position += v.z * m_Forward;
			m_Position += v.y * Vector3::UnitY;
		}

		UpdateViewMatrix();
	}

	void Camera::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_Aspect = static_cast<float>(width) / height;
		UpdateProjMatrix();
	}

	Matrix Camera::GetViewMatrix() const noexcept
	{
		return m_ViewMatrix;
	}

	Matrix Camera::GetProjMatrix() const noexcept
	{
		return m_ProjMatrix;
	}

	void Camera::UpdateProjMatrix() noexcept
	{
		m_ProjMatrix = DirectX::XMMatrixPerspectiveFovLH(
			DirectX::XMConvertToRadians(m_Fov), m_Aspect, m_Near, m_Far);
	}

	void Camera::UpdateViewMatrix() noexcept
	{
		constexpr float PitchLimit = DirectX::XMConvertToRadians(85.0f);
		m_Pitch = std::clamp(m_Pitch, -PitchLimit, +PitchLimit);

		m_Forward = Vector3(std::cos(m_Pitch) * std::sin(m_Yaw), std::sin(m_Pitch), std::cos(m_Pitch) * std::cos(m_Yaw));
		m_Right = Vector3::UnitY.Cross(m_Forward);
		m_Right.Normalize();

		m_Up = m_Forward.Cross(m_Right);
		m_Up.Normalize();

		m_ViewMatrix = DirectX::XMMatrixLookAtLH(m_Position, m_Position + m_Forward, m_Up);

	}
}
