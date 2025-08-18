#pragma once

namespace graphicsGadgetLab
{
	struct Vertex
	{
		Vector3 m_Position = Vector3::Zero;
		Vector3 m_Normal = Vector3::UnitX;
		Vector2 m_TexCoord = Vector2::Zero;
	};
}