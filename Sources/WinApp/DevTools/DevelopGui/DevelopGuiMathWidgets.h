#pragma once
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/Math/Vector.h"

namespace gglab::devtools
{
	void DrawVector3Text(const char* label, const Vector3& value) noexcept;
	void DrawMatrix4x4Tree(const char* label, const Matrix& matrix) noexcept;
}
