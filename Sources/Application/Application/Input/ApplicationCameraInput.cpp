#include "Application/Input/ApplicationCameraInput.h"

namespace gglab
{
	CameraInput BuildCameraInput(const ApplicationInput& input) noexcept
	{
		CameraInput cameraInput{};
		cameraInput.m_Front = input.IsKeyHeld(AppInputKey::W);
		cameraInput.m_Back = input.IsKeyHeld(AppInputKey::S);
		cameraInput.m_Left = input.IsKeyHeld(AppInputKey::A);
		cameraInput.m_Right = input.IsKeyHeld(AppInputKey::D);
		cameraInput.m_Up = input.IsKeyHeld(AppInputKey::Q);
		cameraInput.m_Down = input.IsKeyHeld(AppInputKey::E);
		cameraInput.m_Accelerate = input.IsKeyHeld(AppInputKey::LeftShift);
		cameraInput.m_IsMouseRelative = input.GetPointerMode() == AppPointerMode::Relative;
		const AppInputVector2 pointer = cameraInput.m_IsMouseRelative
			? input.GetPointerDelta()
			: input.GetPointerPosition();
		cameraInput.m_MouseDelta = Vector2(pointer.m_X, pointer.m_Y);
		return cameraInput;
	}
}
