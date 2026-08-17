#pragma once
#include <filesystem>
#include <memory>

namespace gglab
{
	class ShaderCompiler;

	// Process composition boundary for the standalone CLI. Production behavior
	// constructs the normal compiler; the implementation also owns the narrow
	// process-only test access needed by black-box CLI contract tests.
	[[nodiscard]] std::unique_ptr<ShaderCompiler> CreateShaderCompilerForProcess(
		const std::filesystem::path& sourceRoot,
		const std::filesystem::path& cacheRoot) noexcept;
}
