#include "Application/SelfTest/DevelopmentShaderBuildProcessClientSelfTests.h"
#include "Application/Shader/DevelopmentShaderBuildProcessClient.h"
#include "ShaderArtifactRuntime/ShaderCompilerProcessContract.h"

#include <format>
#include <string>

namespace gglab
{
	void RunDevelopmentShaderBuildProcessClientSelfTests(
		SelfTestContext& context) noexcept
	{
		const std::string describe = std::format(
			R"({{"command":"describe","success":true,"status":"ok","exitCode":0,"processContractVersion":{},"compilePolicyRevision":{},"toolIdentity":"gglab-shaderc","toolVersion":"1.1.0","producerKind":"dxc","producerIdentity":"dxc-test","supportedTargets":["gglab-dx12","gglab-vulkan13"],"diagnostics":[]}})",
			ShaderProcessContractVersion, ShaderCompilePolicyRevision);
		context.Check(
			ValidateShaderCompilerDescribeDocument(describe, "gglab-vulkan13").m_Compatible,
			"Development shader client accepts a compatible describe handshake");

		const std::string wrongContract = std::format(
			R"({{"command":"describe","success":true,"status":"ok","exitCode":0,"processContractVersion":{},"compilePolicyRevision":{},"toolIdentity":"gglab-shaderc","toolVersion":"1.1.0","producerKind":"dxc","producerIdentity":"dxc-test","supportedTargets":["gglab-dx12"],"diagnostics":[]}})",
			ShaderProcessContractVersion + 1, ShaderCompilePolicyRevision);
		const std::string wrongCompilePolicy = std::format(
			R"({{"command":"describe","success":true,"status":"ok","exitCode":0,"processContractVersion":{},"compilePolicyRevision":{},"toolIdentity":"gglab-shaderc","toolVersion":"1.1.0","producerKind":"dxc","producerIdentity":"dxc-test","supportedTargets":["gglab-dx12"],"diagnostics":[]}})",
			ShaderProcessContractVersion, ShaderCompilePolicyRevision + 1);
		const std::string duplicateIdentity = std::format(
			R"({{"command":"describe","success":true,"status":"ok","exitCode":0,"processContractVersion":{},"compilePolicyRevision":{},"toolIdentity":"gglab-shaderc","toolIdentity":"other","toolVersion":"1.1.0","producerKind":"dxc","producerIdentity":"dxc-test","supportedTargets":["gglab-dx12"],"diagnostics":[]}})",
			ShaderProcessContractVersion, ShaderCompilePolicyRevision);
		context.Check(
			!ValidateShaderCompilerDescribeDocument(wrongContract, "gglab-dx12").m_Compatible &&
			!ValidateShaderCompilerDescribeDocument(wrongCompilePolicy, "gglab-dx12").m_Compatible &&
			!ValidateShaderCompilerDescribeDocument(duplicateIdentity, "gglab-dx12").m_Compatible &&
			!ValidateShaderCompilerDescribeDocument(describe + "trailing", "gglab-dx12").m_Compatible,
			"Development shader client rejects incompatible policy, duplicate-key, and contaminated handshakes");

		const std::string registryId(64, 'a');
		const std::string build = std::format(
			R"({{"command":"build-runtime","success":true,"status":"ok","exitCode":0,"programCount":53,"diagnostics":[],"registryId":"{}"}})",
			registryId);
		const ShaderCompilerBuildDocumentResult parsed =
			ParseShaderCompilerBuildDocument(build, 0);
		context.Check(parsed.m_ProtocolValid && parsed.m_Succeeded &&
			parsed.m_RegistryRef.IsValid(),
			"Development shader client parses the exact successful build-runtime document");

		const std::string failure =
			R"({"command":"build-runtime","success":false,"status":"failed","exitCode":4,"programCount":0,"diagnostics":[{"message":"compiler unavailable"}]})";
		const ShaderCompilerBuildDocumentResult parsedFailure =
			ParseShaderCompilerBuildDocument(failure, 4);
		context.Check(parsedFailure.m_ProtocolValid && !parsedFailure.m_Succeeded &&
			parsedFailure.m_Diagnostics == "compiler unavailable" &&
			!ParseShaderCompilerBuildDocument(build, 4).m_ProtocolValid &&
			!ParseShaderCompilerBuildDocument(build.substr(0, build.size() - 1), 0).m_ProtocolValid,
			"Development shader client preserves structured failures and rejects exit/document mismatch or truncation");
	}
}
