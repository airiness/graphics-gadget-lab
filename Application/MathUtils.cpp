#include "Precompiled.h"
#include "MathUtils.h"

namespace gglab::utils
{
	Vector4 ToVector4(const Vector3& vec3, float a) noexcept
	{
		return Vector4(vec3.x, vec3.y, vec3.z, a);
	}
}


