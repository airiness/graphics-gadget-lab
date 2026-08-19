#include "ShaderCompilerProcessFactory.h"
#include "Compiler/ShaderCompiler.h"

#include <cstdlib>
#include <memory>

namespace gglab
{
	namespace
	{
		// This process-only switch is deliberately confined to the CLI composition
		// boundary. It lets the black-box process contract cover the otherwise
		// nondeterministic DXC-unavailable outcome without adding a user-facing flag.
		constexpr const wchar_t* ForceCompilerUnavailableEnvironment =
			L"GGLAB_SHADERC_TEST_FORCE_COMPILER_UNAVAILABLE";

		[[nodiscard]] bool ForceCompilerUnavailableForTest() noexcept
		{
			wchar_t value[2]{};
			size_t valueLength = 0;
			return _wgetenv_s(&valueLength, value, 2,
				ForceCompilerUnavailableEnvironment) == 0 &&
				valueLength == 2 && value[0] == L'1';
		}
	}

	std::unique_ptr<ShaderCompiler> CreateShaderCompilerForProcess(
		const std::filesystem::path& sourceRoot,
		const std::filesystem::path& cacheRoot) noexcept
	{
		return ForceCompilerUnavailableForTest()
			? ShaderCompiler::MakeUnavailable(sourceRoot, cacheRoot)
			: std::make_unique<ShaderCompiler>(sourceRoot, cacheRoot);
	}
}
