#pragma once

namespace gglab
{
	// The gglab-shaderc tool version, carried on the describe wire and emitted
	// by --version. Bumped when the tool's user-visible behavior changes:
	// describe wire facts, compile/build-runtime behavior, target set, exit
	// codes. Consumers and tests must compare against this constant rather
	// than hard-code a value.
	inline constexpr const wchar_t* ShaderCompilerToolVersion = L"1.1.0";

	// The machine describe handshake process-contract wire version.
	// Deliberately independent of ShaderCompilerToolVersion: it versions the
	// "document format + status vocabulary + exit-code mapping +
	// stdout/stderr channel rules" of the whole machine process contract.
	// Future contract changes bump this from the same constant so the value
	// in the describe document and the consumer support-set gate stay in
	// lockstep.
	inline constexpr int ShaderProcessContractVersion = 1;
}
