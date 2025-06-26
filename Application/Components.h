#pragma once

namespace graphicsGadgetLab
{
	namespace Component
	{
		struct Transform
		{
			Vector3 m_Position = Vector3::Zero;
			Quaternion m_Rotation = Quaternion::Identity;
			Vector3 m_Scale = Vector3::One;
		};
	}
}