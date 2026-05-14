#include "Core/Precompiled.h"
#include "Graphics/Camera.h"

namespace gglab
{
	Camera::Camera(const CreateInfo& info) noexcept :
		m_Forward(info.m_Forward),
		m_Position(info.m_Position),
		m_Near(info.m_Near),
		m_Far(info.m_Far),
		m_Fov(info.m_Fov)
	{
		// sanitize
		m_Near = ClampNear(m_Near);
		m_Far = ClampFar(m_Near, m_Far);
		m_Fov = ClampFov(m_Fov);

		auto width = std::max(1u, info.m_Width);
		auto height = std::max(1u, info.m_Height);
		m_Aspect = static_cast<float>(width) / static_cast<float>(height);

		ComputeYawPitchFromForward(m_Forward);

		UpdateViewMatrix();
		UpdateProjMatrix();

		m_ViewDirty = false;
		m_ProjDirty = false;
	}

	void Camera::Update() noexcept
	{
		if (m_ViewDirty)
		{
			UpdateViewMatrix();
			m_ViewDirty = false;
		}

		if (m_ProjDirty)
		{
			UpdateProjMatrix();
			m_ProjDirty = false;
		}
	}

	void Camera::SetPosition(const Vector3& pos) noexcept
	{
		m_Position = pos;
		MarkViewDirty();
	}

	void Camera::SetYawPitch(float yawRadians, float pitchRadians) noexcept
	{
		if (!utils::IsFinite(yawRadians) ||
			!utils::IsFinite(pitchRadians))
		{
			return;
		}

		m_Yaw = yawRadians;
		m_Pitch = std::clamp(pitchRadians, -PitchLimitRadians, +PitchLimitRadians);

		MarkViewDirty();
	}

	void Camera::SetNearFar(float nearZ, float farZ) noexcept
	{
		if (!utils::IsFinite(nearZ) ||
			!utils::IsFinite(farZ))
		{
			return;
		}

		m_Near = ClampNear(nearZ);
		m_Far = ClampFar(m_Near, farZ);

		MarkProjDirty();
	}

	void Camera::SetFov(float fovDegrees) noexcept
	{
		if (!utils::IsFinite(fovDegrees))
		{
			return;
		}

		m_Fov = ClampFov(fovDegrees);
		MarkProjDirty();
	}

	void Camera::SetAspect(float aspect) noexcept
	{
		if (!utils::IsFinite(aspect) || aspect <= 0.0f)
		{
			return;
		}

		m_Aspect = aspect;
		MarkProjDirty();
	}

	void Camera::OnResize(uint32_t width, uint32_t height) noexcept
	{
		width = std::max(1u, width);
		height = std::max(1u, height);
		m_Aspect = static_cast<float>(width) / static_cast<float>(height);
		MarkProjDirty();
	}

	void Camera::LookAt(const Vector3& eye, const Vector3& target) noexcept
	{
		m_Position = eye;

		Vector3 forward = utils::SafeNormalize(target - eye, Vector3::UnitZ);
		ComputeYawPitchFromForward(forward);

		MarkViewDirty();
	}

	float Camera::ClampNear(float nearZ) noexcept
	{
		return std::max(0.0001f, nearZ);
	}

	float Camera::ClampFar(float nearZ, float farZ) noexcept
	{
		return std::max(nearZ + 0.0001f, farZ);
	}

	float Camera::ClampFov(float fov) noexcept
	{
		return std::clamp(fov, 1.0f, 179.0f);
	}

	void Camera::UpdateProjMatrix() noexcept
	{
		m_ProjMatrix = DirectX::XMMatrixPerspectiveFovLH(
			utils::ToRadians(m_Fov), m_Aspect, m_Near, m_Far);
	}

	void Camera::UpdateViewMatrix() noexcept
	{
		// normalize yaw to [-PI, PI]
		m_Yaw = std::remainder(m_Yaw, DirectX::XM_2PI);
		m_Pitch = std::clamp(m_Pitch, -PitchLimitRadians, +PitchLimitRadians);

		// forward = (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))
		const float cosP = std::cos(m_Pitch);
		m_Forward = Vector3(cosP * std::sin(m_Yaw), std::sin(m_Pitch), cosP * std::cos(m_Yaw));

		m_Right = Vector3::UnitY.Cross(m_Forward);
		m_Right = utils::SafeNormalize(m_Right, Vector3::UnitX);

		m_Up = m_Forward.Cross(m_Right);
		m_Up = utils::SafeNormalize(m_Up, Vector3::UnitY);

		m_ViewMatrix = DirectX::XMMatrixLookAtLH(m_Position, m_Position + m_Forward, m_Up);
	}

	void Camera::ComputeYawPitchFromForward(const Vector3& forward) noexcept
	{
		// forward is expected normalized
		// pitch = asin(y)
		// yaw = atan2(x, z)
		const Vector3 normForward = utils::SafeNormalize(forward, Vector3::UnitZ);
		auto pitch = std::asin(std::clamp(normForward.y, -1.0f, 1.0f));
		m_Yaw = std::atan2(normForward.x, normForward.z);

		m_Pitch = std::clamp(pitch, -PitchLimitRadians, +PitchLimitRadians);
	}
}