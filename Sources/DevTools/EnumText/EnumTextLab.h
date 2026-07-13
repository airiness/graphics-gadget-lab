#pragma once
#include "Application/Lab/LabTypes.h"
#include "DevTools/EnumText/EnumText.h"

namespace gglab::devtools
{
	template<>
	struct EnumTextTraits<LabRunState>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ LabRunState::Uninitialized, "Uninitialized" },
			EnumTextEntry{ LabRunState::Loading, "Loading" },
			EnumTextEntry{ LabRunState::WarmingUp, "Warming Up" },
			EnumTextEntry{ LabRunState::Ready, "Ready" },
			EnumTextEntry{ LabRunState::Capturing, "Capturing" },
			EnumTextEntry{ LabRunState::Completed, "Completed" },
			EnumTextEntry{ LabRunState::Failed, "Failed" },
		};
	};
}
