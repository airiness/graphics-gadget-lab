#include "Core/Precompiled.h"
#include "Graphics/Asset/Interop/AssimpMathInterop.h"

namespace gglab::math::interop
{
	Matrix FromAssimp(const aiMatrix4x4& matrix) noexcept
	{
		// Assimp transforms column vectors; gglab transforms row vectors.
		return Matrix(
			matrix.a1, matrix.b1, matrix.c1, matrix.d1,
			matrix.a2, matrix.b2, matrix.c2, matrix.d2,
			matrix.a3, matrix.b3, matrix.c3, matrix.d3,
			matrix.a4, matrix.b4, matrix.c4, matrix.d4);
	}
}
