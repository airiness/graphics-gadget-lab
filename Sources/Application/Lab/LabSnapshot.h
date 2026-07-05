#pragma once
#include "Application/Lab/LabParameter.h"
#include "Application/Lab/LabTypes.h"

namespace gglab
{
	struct LabParameterSnapshot
	{
		LabParameterDesc m_Desc;
		LabValue m_Value = false;
	};

	struct LabSnapshot
	{
		std::vector<LabDescriptor> m_AvailableLabs;
		LabId m_ActiveLabId;
		std::string m_ActiveLabName;
		std::string m_Category;
		std::string m_Description;
		uint32_t m_SchemaVersion = 0;
		LabRuntimeState m_State = LabRuntimeState::Uninitialized;
		uint64_t m_FrameInSession = 0;
		std::vector<LabParameterSnapshot> m_Parameters;
		std::string m_LastError;
		bool m_HasPendingCommands = false;
		bool m_IsHostActive = false;
	};
}
