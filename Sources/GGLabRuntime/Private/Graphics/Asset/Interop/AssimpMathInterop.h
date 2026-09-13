#pragma once
#include "GGLabRuntime/Core/Math/Matrix.h"

#include <assimp/matrix4x4.h>

namespace gglab::math::interop
{
	Matrix FromAssimp(const aiMatrix4x4& matrix) noexcept;
}
