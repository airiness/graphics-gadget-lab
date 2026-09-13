#pragma once

#include "ApplicationInput.h"
#include "GGLabRuntime/Graphics/CameraController.h"

namespace gglab
{
	[[nodiscard]] CameraInput BuildCameraInput(const ApplicationInput& input) noexcept;
}
