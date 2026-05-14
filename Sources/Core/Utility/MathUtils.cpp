#include "Core/Precompiled.h"
#include "Core/Utility/MathUtils.h"

namespace gglab::utils
{
	Vector4 ToVector4(const Vector3& vec3, float a) noexcept
	{
		return { vec3.x, vec3.y, vec3.z, a };
	}
}