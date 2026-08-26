#pragma once
#include <filesystem>
#include <memory>

namespace gglab
{
	class ShaderCompiler;
	struct ShaderCompilerIdentity;

	// Process composition boundary for the standalone CLI. Production behavior
	// constructs the normal compiler and observes the active producer
	// identity through the core query. The implementation owns the narrow
	// process-only test transport needed by black-box CLI contract tests, and
	// exposes it only as semantic facts below: a forced-unavailable producer
	// observation (consumed by every command that asks about the producer)
	// and a describe-only forced internal fault. Commands never parse the
	// test transport themselves.

	// The compiler instance for this process: the normal compiler in
	// production; the forced-unavailable observation in the black-box
	// transport.
	[[nodiscard]] std::unique_ptr<ShaderCompiler> CreateShaderCompilerForProcess(
		const std::filesystem::path& sourceRoot,
		const std::filesystem::path& cacheRoot) noexcept;

	// The active producer identity as observed at this process boundary.
	// Consumers apply their normal production judgments on it (an
	// unresolvable identity maps to the structured compiler-unavailable
	// failure).
	[[nodiscard]] ShaderCompilerIdentity QueryShaderCompilerIdentityForProcess() noexcept;

	// Describe-only: the forced handled internal-failure fault. An
	// operation-specific fault injection that never applies to any other
	// command.
	[[nodiscard]] bool ShouldForceDescribeInternalErrorForTest() noexcept;
}
