#include "Core/Precompiled.h"
#include "Core/Math/Transform.h"

namespace gglab::math
{
	Matrix CreateTransformMatrix(
		const Vector3& scale,
		const Quaternion& rotation,
		const Vector3& translation) noexcept
	{
		return CreateScale(scale) *
			CreateFromQuaternion(rotation) *
			CreateTranslation(translation);
	}

	Matrix CreateNormalMatrix(const Matrix& transform) noexcept
	{
		Matrix linearTransform = transform;
		linearTransform.Translation(Vector3::Zero);
		return Transpose(SafeInverse(linearTransform));
	}
}
