#pragma once
#include <cstdint>
#include <string_view>

namespace gglab
{
	// Stable machine identity used by process clients before they consume any
	// command result from the external shader producer.
	inline constexpr std::string_view ShaderCompilerToolIdentity = "gglab-shaderc";
	inline constexpr const wchar_t* ShaderCompilerToolVersion = L"1.2.0";

	// Versions the JSON envelope, status vocabulary, exit-code mapping, and
	// stdout/stderr channel rules. This is deliberately independent of the
	// user-visible tool version.
	inline constexpr int ShaderProcessContractVersion = 2;

	// Independent compatibility axis for Preview request, result, and status
	// semantics. It is not part of the process-v2 describe document and does not
	// change compatibility for existing compiler operations.
	inline constexpr int ShaderPreviewBuildContractVersion = 1;

	// Independent compatibility axis for compiler-owned policy. Bump this
	// whenever argument generation, lowering, or another producer policy can
	// change emitted binaries without changing the normalized recipe or DXC
	// identity. It participates in both BuildKey derivation and the external
	// compiler handshake.
	inline constexpr uint32_t ShaderCompilePolicyRevision = 1;
}
