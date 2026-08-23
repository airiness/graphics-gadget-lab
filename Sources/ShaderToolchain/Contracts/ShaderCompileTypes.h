#pragma once
#include "ShaderArtifactRuntime/ShaderArtifactTypes.h"

#include <string>

namespace gglab
{
	struct ShaderDefine
	{
		std::wstring m_Name{};
		std::wstring m_Value{};

		bool operator==(const ShaderDefine&) const noexcept = default;
		auto operator<=>(const ShaderDefine&) const noexcept = default;
	};
}
