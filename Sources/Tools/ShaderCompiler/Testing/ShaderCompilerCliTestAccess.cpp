#include "ShaderCompilerProcessFactory.h"
#include "Compiler/ShaderCompiler.h"

#include <cstdlib>
#include <memory>

namespace gglab
{
	namespace
	{
		// All GGLAB_SHADERC_TEST_* transport reads are confined to this
		// translation unit: the commands above (compile / build-runtime /
		// describe / version) consume the semantic facts exposed in
		// ShaderCompilerProcessFactory.h and never parse the test transport
		// themselves.
		constexpr const wchar_t* ForceUnavailableEnvironment =
			L"GGLAB_SHADERC_TEST_FORCE_COMPILER_UNAVAILABLE";
		constexpr const wchar_t* ForceDescribeInternalErrorEnvironment =
			L"GGLAB_SHADERC_TEST_FORCE_DESCRIBE_INTERNAL_ERROR";

		[[nodiscard]] bool EnvironmentForces(const wchar_t* environment) noexcept
		{
			wchar_t value[2]{};
			size_t valueLength = 0;
			return _wgetenv_s(&valueLength, value, 2, environment) == 0 &&
				valueLength == 2 && value[0] == L'1';
		}
	}

	// The active producer identity as observed at this process boundary. When
	// the black-box transport forces the producer unavailable, the observation
	// mirrors the core query's real unresolvable-sentinel outcome, and the
	// consuming commands (describe / version) take their normal
	// unresolvable-identity judgments from it.
	[[nodiscard]] ShaderCompilerIdentity QueryShaderCompilerIdentityForProcess() noexcept
	{
		if (EnvironmentForces(ForceUnavailableEnvironment))
		{
			ShaderCompilerIdentity identity{};
			identity.m_CanonicalIdentity = L"unknown";
			return identity;
		}
		return QueryDxcCompilerIdentity();
	}

	// Describe-only operation-specific fault: force the handled internal-
	// failure path. It never applies to any other command.
	[[nodiscard]] bool ShouldForceDescribeInternalErrorForTest() noexcept
	{
		return EnvironmentForces(ForceDescribeInternalErrorEnvironment);
	}

	// The compiler instance for this process. The same forced-unavailability
	// observation as the producer-identity seam: a process without a producer
	// creates an unavailable compiler, and compile / build-runtime keep their
	// normal production judgments on it.
	std::unique_ptr<ShaderCompiler> CreateShaderCompilerForProcess(
		const std::filesystem::path& sourceRoot,
		const std::filesystem::path& cacheRoot) noexcept
	{
		return EnvironmentForces(ForceUnavailableEnvironment)
			? ShaderCompiler::MakeUnavailable(sourceRoot, cacheRoot)
			: std::make_unique<ShaderCompiler>(sourceRoot, cacheRoot);
	}
}
