#include "Precompiled.h"
#include "CameraController.h"
#include "Camera.h"
#include "MathUtils.h"

namespace gglab
{
	CameraController::CameraController(const CreateInfo& createInfo) noexcept :
		m_Params(Sanitize(createInfo.m_Params))
	{}

	void CameraController::Update(Camera& camera, const CameraInput& input, float deltaTime) noexcept
	{
		if (deltaTime <= 0.0f)
		{
			return;
		}

		// Mouse relative mode, process yaw and pitch
		if (input.m_IsMouseRelative)
		{
			const float yawDelta = input.m_MouseDelta.x * m_Params.m_MouseSensitivityRadPerCount;
			const float pitchDelta = (-input.m_MouseDelta.y) * m_Params.m_MouseSensitivityRadPerCount;

			camera.SetYawPitch(camera.GetYaw() + yawDelta, camera.GetPitch() + pitchDelta);
		}

		// Movement input in local intent space: (x=right, y=up, z=forward)
		const Vector3 moveIntent = BuildMovementVector(input);

		// Build world-space movement axes:
		const Vector3 worldUp = Vector3::UnitY;

		Vector3 forwardXZ(camera.GetForward().x, 0.0f, camera.GetForward().z);
		forwardXZ = utils::SafeNormalize(forwardXZ, Vector3::UnitZ);

		const Vector3 right = camera.GetRight();

		// Desired direction
		Vector3 desiredDir =
			right * moveIntent.x +
			worldUp * moveIntent.y +
			forwardXZ * moveIntent.z;

		// Normalize desiredDir
		desiredDir = utils::SafeNormalize(desiredDir, Vector3::Zero);

		float speed = m_Params.m_MovementSpeed;
		if (input.m_Accelerate)
		{
			speed *= m_Params.m_AccelerateMultiplier;
		}

		const Vector3 desiredVelocity = desiredDir * speed;

		// Velocity smoothing (exponential approach)
		// m_SmoothStepT: 0 => no smoothing (snap), 1 => strongest following (still snaps due to alpha=1)
		const float t = m_Params.m_SmoothStepT;
		if (t <= 0.0f)
		{
			m_Velocity = desiredVelocity;
		}
		else
		{
			// Convert per-frame interpolation factor to frame-rate independent alpha.
			float alpha = 1.0f - std::pow(1.0f - std::clamp(t, 0.0f, 1.0f), deltaTime * 60.0f);
			alpha = std::clamp(alpha, 0.0f, 1.0f);
			m_Velocity = Vector3::Lerp(m_Velocity, desiredVelocity, alpha);
		}

		// Integrate position
		const Vector3 newPos = camera.GetPosition() + m_Velocity * deltaTime;
		camera.SetPosition(newPos);
	}

	CameraController::Params CameraController::Sanitize(Params params) noexcept
	{
		params.m_MovementSpeed = std::max(0.0f, params.m_MovementSpeed);
		params.m_MouseSensitivityRadPerCount = std::max(0.0f, params.m_MouseSensitivityRadPerCount);
		params.m_AccelerateMultiplier = std::max(1.0f, params.m_AccelerateMultiplier);
		params.m_SmoothStepT = std::clamp(params.m_SmoothStepT, 0.0f, 1.0f);
		return params;
	}

	Vector3 CameraController::BuildMovementVector(const CameraInput& input) noexcept
	{
		// x: right/left, y: up/down, z: forward/back
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;

		if (input.m_Right) { x += 1.0f; }
		if (input.m_Left) { x -= 1.0f; }

		if (input.m_Up) { y += 1.0f; }
		if (input.m_Down) { y -= 1.0f; }

		if (input.m_Front) { z += 1.0f; }
		if (input.m_Back) { z -= 1.0f; }

		return Vector3(x, y, z);
	}
}