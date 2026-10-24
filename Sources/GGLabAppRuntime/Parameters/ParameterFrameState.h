#pragma once
#include "Lab/LabParameter.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "GGLabRuntime/Graphics/LightingProfile.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	struct ParameterFrameValue
	{
		std::string m_Id;
		std::string m_Source;
		LabParameterType m_Type = LabParameterType::Bool;
		LabValue m_Value = false;
		LabValue m_Default = false;
	};

	struct ParameterFrameLighting
	{
		uint64_t m_Generation = 0;
		std::optional<LightingProfileState> m_Profile;
	};

	struct ParameterFrameReceipt
	{
		uint64_t m_BatchId = 0;
		ParameterEffectSnapshot m_Item;
	};

	// Owned build-time inputs and domain identities. Submission may only complete
	// these exact receipt items; later requests never relabel an earlier frame.
	struct ParameterFrameState
	{
		uint64_t m_FrameSerial = 0;
		uint64_t m_Revision = 0;
		std::string m_ContentId;
		bool m_Available = false;
		std::vector<ParameterFrameValue> m_Values;
		ParameterFrameLighting m_RequestedLighting;
		ParameterFrameLighting m_ActiveLighting;
		std::vector<ParameterFrameReceipt> m_Receipts;
	};
}
