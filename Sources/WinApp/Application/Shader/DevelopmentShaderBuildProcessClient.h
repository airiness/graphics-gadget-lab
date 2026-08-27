#pragma once
#include "Application/Shader/DevelopmentShaderBuildBridge.h"

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace gglab
{
	struct ShaderCompilerDescribeValidationResult final
	{
		bool m_Compatible = false;
		std::string m_Diagnostics{};
	};

	struct ShaderCompilerBuildDocumentResult final
	{
		bool m_ProtocolValid = false;
		bool m_Succeeded = false;
		ShaderProgramRegistryArtifactRef m_RegistryRef{};
		std::string m_Diagnostics{};
	};

	[[nodiscard]] ShaderCompilerDescribeValidationResult
		ValidateShaderCompilerDescribeDocument(
			std::string_view document, std::string_view requiredTarget) noexcept;

	[[nodiscard]] ShaderCompilerBuildDocumentResult
		ParseShaderCompilerBuildDocument(
			std::string_view document, uint32_t processExitCode) noexcept;

	[[nodiscard]] DevelopmentShaderBuildResult RunDevelopmentShaderBuildProcess(
		const DevelopmentShaderBuildRequest& request,
		std::stop_token stopToken = {}) noexcept;
}
