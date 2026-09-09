#pragma once

namespace gglab
{
	struct CameraControllerSettings
	{
		float m_MovementSpeed = 10.0f;
		float m_MouseSensitivityRadPerCount = 0.001f;
		float m_AccelerateMultiplier = 3.0f;
		float m_SmoothStepT = 0.5f;
	};
}
