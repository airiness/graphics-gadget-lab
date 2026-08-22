#pragma once

#include "ApplicationInput.h"
#include "Graphics/CameraController.h"

namespace gglab
{
	[[nodiscard]] CameraInput BuildCameraInput(const ApplicationInput& input) noexcept;
}
