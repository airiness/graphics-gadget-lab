#pragma once
#include <string_view>

namespace gglab
{
	// Stable machine identity used by process clients before they consume any
	// command result from the external shader producer.
	inline constexpr std::string_view ShaderCompilerToolIdentity = "gglab-shaderc";
	inline constexpr const wchar_t* ShaderCompilerToolVersion = L"1.1.0";

	// Versions the JSON envelope, status vocabulary, exit-code mapping, and
	// stdout/stderr channel rules. This is deliberately independent of the
	// user-visible tool version.
	inline constexpr int ShaderProcessContractVersion = 1;
}
