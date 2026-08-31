#include "Application/SelfTest/DevelopmentShaderBuildProcessClientSelfTests.h"
#include "Application/Shader/DevelopmentShaderBuildProcessClient.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "ShaderArtifactRuntime/ShaderCompilerProcessContract.h"

#include <format>
#include <string>

namespace gglab
{
	void RunDevelopmentShaderBuildProcessClientSelfTests(
		SelfTestContext& context) noexcept
	{
		const std::string toolIdentity(ShaderCompilerToolIdentity);
		const std::string toolVersion = utils::ToString(ShaderCompilerToolVersion);
		const std::string describe = std::format(
			R"({{"command":"describe","success":true,"status":"ok","exitCode":0,"processContractVersion":{},"compilePolicyRevision":{},"toolIdentity":"{}","toolVersion":"{}","producerKind":"dxc","producerIdentity":"dxc-test","supportedTargets":["gglab-dx12","gglab-vulkan13"],"diagnostics":[]}})",
			ShaderProcessContractVersion, ShaderCompilePolicyRevision,
			toolIdentity, toolVersion);
		const ShaderCompilerDescribeDocumentResult parsedDescribe =
			ParseShaderCompilerDescribeDocument(describe, 0, "gglab-vulkan13");
		context.Check(
			parsedDescribe.m_ProtocolValid && parsedDescribe.m_Succeeded &&
			parsedDescribe.m_Compatible,
			"Development shader client accepts a compatible describe handshake");

		const std::string describeWithInformationalFields = std::format(
			R"({{"command":"describe","success":true,"status":"ok","exitCode":0,"processContractVersion":{},"compilePolicyRevision":{},"toolIdentity":"{}","toolVersion":"{}","producerKind":"dxc","producerIdentity":"dxc-test","supportedTargets":["gglab-dx12"],"diagnostics":[],"futureInfo":{{"revision":7}},"traceTags":["ignored"]}})",
			ShaderProcessContractVersion, ShaderCompilePolicyRevision,
			toolIdentity, toolVersion);
		const ShaderCompilerDescribeDocumentResult parsedDescribeWithInformationalFields =
			ParseShaderCompilerDescribeDocument(
				describeWithInformationalFields, 0, "gglab-dx12");
		context.Check(
			parsedDescribeWithInformationalFields.m_ProtocolValid &&
			parsedDescribeWithInformationalFields.m_Succeeded &&
			parsedDescribeWithInformationalFields.m_Compatible,
			"Development shader client ignores unknown informational describe fields");

		const std::string missingRequiredField = std::format(
			R"({{"command":"describe","success":true,"status":"ok","exitCode":0,"processContractVersion":{},"compilePolicyRevision":{},"toolIdentity":"{}","producerKind":"dxc","producerIdentity":"dxc-test","supportedTargets":["gglab-dx12"],"diagnostics":[]}})",
			ShaderProcessContractVersion, ShaderCompilePolicyRevision, toolIdentity);
		const std::string invalidRequiredFieldType = std::format(
			R"({{"command":"describe","success":true,"status":"ok","exitCode":0,"processContractVersion":{},"compilePolicyRevision":"{}","toolIdentity":"{}","toolVersion":"{}","producerKind":"dxc","producerIdentity":"dxc-test","supportedTargets":["gglab-dx12"],"diagnostics":[]}})",
			ShaderProcessContractVersion, ShaderCompilePolicyRevision,
			toolIdentity, toolVersion);
		context.Check(
			!ParseShaderCompilerDescribeDocument(
				missingRequiredField, 0, "gglab-dx12").m_ProtocolValid &&
			!ParseShaderCompilerDescribeDocument(
				invalidRequiredFieldType, 0, "gglab-dx12").m_ProtocolValid,
			"Development shader client keeps required describe fields strict");

		const std::string wrongContract = std::format(
			R"({{"command":"describe","success":true,"status":"ok","exitCode":0,"processContractVersion":{},"compilePolicyRevision":{},"toolIdentity":"{}","toolVersion":"{}","producerKind":"dxc","producerIdentity":"dxc-test","supportedTargets":["gglab-dx12"],"diagnostics":[]}})",
			ShaderProcessContractVersion + 1, ShaderCompilePolicyRevision,
			toolIdentity, toolVersion);
		const std::string wrongCompilePolicy = std::format(
			R"({{"command":"describe","success":true,"status":"ok","exitCode":0,"processContractVersion":{},"compilePolicyRevision":{},"toolIdentity":"{}","toolVersion":"{}","producerKind":"dxc","producerIdentity":"dxc-test","supportedTargets":["gglab-dx12"],"diagnostics":[]}})",
			ShaderProcessContractVersion, ShaderCompilePolicyRevision + 1,
			toolIdentity, toolVersion);
		const std::string duplicateIdentity = std::format(
			R"({{"command":"describe","success":true,"status":"ok","exitCode":0,"processContractVersion":{},"compilePolicyRevision":{},"toolIdentity":"{}","toolIdentity":"other","toolVersion":"{}","producerKind":"dxc","producerIdentity":"dxc-test","supportedTargets":["gglab-dx12"],"diagnostics":[]}})",
			ShaderProcessContractVersion, ShaderCompilePolicyRevision,
			toolIdentity, toolVersion);
		context.Check(
			!ParseShaderCompilerDescribeDocument(wrongContract, 0, "gglab-dx12").m_ProtocolValid &&
			!ParseShaderCompilerDescribeDocument(wrongCompilePolicy, 0, "gglab-dx12").m_Compatible &&
			!ParseShaderCompilerDescribeDocument(duplicateIdentity, 0, "gglab-dx12").m_ProtocolValid &&
			!ParseShaderCompilerDescribeDocument(describe + "trailing", 0, "gglab-dx12").m_ProtocolValid,
			"Development shader client rejects incompatible policy, duplicate-key, and contaminated handshakes");

		const std::string describeFailure = std::format(
			R"({{"command":"describe","success":false,"status":"compiler-unavailable","exitCode":4,"processContractVersion":{},"diagnostics":[{{"message":"DXC unavailable","category":"environment"}}],"futureInfo":true}})",
			ShaderProcessContractVersion);
		const ShaderCompilerDescribeDocumentResult parsedDescribeFailure =
			ParseShaderCompilerDescribeDocument(describeFailure, 4, "gglab-dx12");
		context.Check(
			parsedDescribeFailure.m_ProtocolValid && !parsedDescribeFailure.m_Succeeded &&
			!parsedDescribeFailure.m_Compatible &&
			parsedDescribeFailure.m_Diagnostics == "DXC unavailable" &&
			!ParseShaderCompilerDescribeDocument(
				describeFailure, 7, "gglab-dx12").m_ProtocolValid,
			"Development shader client accepts informational failure fields and rejects exit mismatch");

		const DevelopmentShaderBuildRequest unknownBackendRequest{
			.m_ActiveBackend = static_cast<RHIBackendType>(0xff),
			.m_ShaderCompilerPath = L"C:\\gglab-shaderc.exe",
			.m_ShaderSourceRoot = L"C:\\Shaders",
			.m_ShaderCacheRoot = L"C:\\ShaderCache",
			.m_ArtifactRoot = L"C:\\ShaderArtifacts",
		};
		const DevelopmentShaderBuildResult unknownBackend =
			RunDevelopmentShaderBuildProcess(unknownBackendRequest);
		context.Check(
			!unknownBackendRequest.IsValid() &&
			unknownBackend.m_Status == DevelopmentShaderBuildStatus::InvalidInput,
			"Development shader client rejects undeclared backends instead of defaulting to DX12");

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
