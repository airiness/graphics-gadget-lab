#pragma once
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"

namespace gglab::math
{
	Matrix CreateTransformMatrix(
		const Vector3& scale, const Quaternion& rotation, const Vector3& translation) noexcept;

	Matrix CreateNormalMatrix(const Matrix& transform) noexcept;
}
