#include "Core/Math/Matrix.h"

namespace gglab::math
{
	const Matrix Matrix::Identity = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
	};

	Vector3 Matrix::Translation() const noexcept
	{
		return Vector3(m_41, m_42, m_43);
	}

	void Matrix::Translation(const Vector3& value) noexcept
	{
		m_41 = value.m_X;
		m_42 = value.m_Y;
		m_43 = value.m_Z;
	}
}
