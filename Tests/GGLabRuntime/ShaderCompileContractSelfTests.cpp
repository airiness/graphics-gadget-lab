#include "ShaderCompileContractSelfTests.h"
#include "Testing/ShaderArtifactManifestIOTestAccess.h"

#include "SpirVDecorationReader.h"
#include "Artifact/ShaderArtifactManifestIO.h"
#include "Artifact/ShaderRuntimeArtifactPublication.h"
#include "Compiler/ShaderCompiler.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "Graphics/RHI/RHICoordinatePolicy.h"
#include "Graphics/RHI/Vulkan/VulkanCoordinatePolicy.h"
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"
#include "DevelopmentShaderPaths.h"
#include "Targets/Vulkan13ShaderTarget.h"
#include "ShaderArtifactRuntime/VulkanShaderRuntimeABI.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		class ScopedTestDirectory
		{
		public:
			explicit ScopedTestDirectory(std::filesystem::path path) noexcept : m_Path(std::move(path))
			{
				std::error_code errorCode;
				std::filesystem::remove_all(m_Path, errorCode);
			}
			~ScopedTestDirectory()
			{
				std::error_code errorCode;
				std::filesystem::remove_all(m_Path, errorCode);
			}

			const std::filesystem::path& GetPath() const noexcept { return m_Path; }

		private:
			std::filesystem::path m_Path;
		};

		[[nodiscard]] bool ContainsArgument(
			const std::vector<std::wstring>& arguments, std::wstring_view expected) noexcept
		{
			return std::ranges::find(arguments, expected) != arguments.end();
		}

		[[nodiscard]] bool ContainsArgumentSequence(const std::vector<std::wstring>& arguments,
			std::initializer_list<std::wstring_view> expected) noexcept
		{
			if (expected.size() > arguments.size())
			{
				return false;
			}
			for (size_t offset = 0; offset + expected.size() <= arguments.size(); ++offset)
			{
				bool matches = true;
				size_t index = offset;
				for (std::wstring_view value : expected)
				{
					matches &= arguments[index++] == value;
				}
				if (matches)
				{
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool HasDescriptorBinding(const SpirVDecorationReflection& reflection,
			uint32_t descriptorSet, uint32_t binding) noexcept
		{
			return std::ranges::any_of(reflection.m_DescriptorBindings,
				[descriptorSet, binding](const SpirVDescriptorBindingReflection& descriptor) noexcept
				{
					return descriptor.m_DescriptorSet == descriptorSet &&
						descriptor.m_Binding == binding;
				});
		}

		[[nodiscard]] ShaderBinary MakeExecutionModelModule(uint32_t executionModel) noexcept
		{
			const std::array<uint32_t, 10> words{
				0x07230203u,
				0x00010600u,
				0u,
				2u,
				0u,
				(5u << 16) | 15u,
				executionModel,
				1u,
				0x6e69616du,
				0u,
			};
			ShaderBinary binary(sizeof(words));
			std::memcpy(binary.Data(), words.data(), sizeof(words));
			return binary;
		}

		[[nodiscard]] bool OverwriteBinaryFile(
			const std::filesystem::path& path, const ShaderBinary& binary) noexcept
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(static_cast<const char*>(binary.Data()),
				static_cast<std::streamsize>(binary.SizeInBytes()));
			return output.good();
		}

		[[nodiscard]] bool OverwriteTextFile(
			const std::filesystem::path& path, std::string_view content) noexcept
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(content.data(), static_cast<std::streamsize>(content.size()));
			return output.good();
		}

		[[nodiscard]] std::filesystem::path MakeCacheRecordPath(
			const std::filesystem::path& binaryPath) noexcept
		{
			auto recordPath = binaryPath;
			recordPath += L".json";
			return recordPath;
		}

		constexpr std::wstring_view VulkanSdkValidationBaseline = L"1.3.296.0";
		constexpr std::string_view SpirVToolsValidationBaseline =
			"SPIRV-Tools v2024.4 v2024.4.rc1-0-g6dcc7e35";

		struct SpirVValidatorInfo
		{
			std::filesystem::path m_Path;
			std::wstring m_SdkVersion;
			std::string m_ToolIdentity;

			[[nodiscard]] bool MatchesValidationBaseline() const noexcept
			{
				return !m_Path.empty() && m_SdkVersion == VulkanSdkValidationBaseline &&
					m_ToolIdentity.starts_with(SpirVToolsValidationBaseline);
			}
		};

		[[nodiscard]] bool QuerySpirVValidatorIdentity(
			const std::filesystem::path& validator, std::string& outIdentity) noexcept
		{
			outIdentity.clear();
			SECURITY_ATTRIBUTES securityAttributes{
				.nLength = sizeof(SECURITY_ATTRIBUTES),
				.bInheritHandle = TRUE,
			};
			HANDLE readPipe = nullptr;
			HANDLE writePipe = nullptr;
			if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0))
			{
				return false;
			}
			if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))
			{
				CloseHandle(writePipe);
				CloseHandle(readPipe);
				return false;
			}

			std::wstring commandLine = std::format(L"\"{}\" --version", validator.wstring());
			STARTUPINFOW startupInfo{
				.cb = sizeof(STARTUPINFOW),
				.dwFlags = STARTF_USESTDHANDLES,
				.hStdInput = GetStdHandle(STD_INPUT_HANDLE),
				.hStdOutput = writePipe,
				.hStdError = writePipe,
			};
			PROCESS_INFORMATION processInfo{};
			const BOOL created = CreateProcessW(validator.c_str(), commandLine.data(), nullptr,
				nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo);
			CloseHandle(writePipe);
			if (!created)
			{
				CloseHandle(readPipe);
				return false;
			}

			const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 30'000);
			if (waitResult == WAIT_TIMEOUT)
			{
				TerminateProcess(processInfo.hProcess, ERROR_TIMEOUT);
				WaitForSingleObject(processInfo.hProcess, INFINITE);
			}

			std::array<char, 1'024> buffer{};
			DWORD bytesRead = 0;
			while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()),
				&bytesRead, nullptr) && bytesRead > 0)
			{
				outIdentity.append(buffer.data(), bytesRead);
			}

			DWORD exitCode = ERROR_GEN_FAILURE;
			GetExitCodeProcess(processInfo.hProcess, &exitCode);
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			CloseHandle(readPipe);
			return waitResult == WAIT_OBJECT_0 && exitCode == 0 && !outIdentity.empty();
		}

		[[nodiscard]] SpirVValidatorInfo FindSpirVValidator() noexcept
		{
			SpirVValidatorInfo info{};
			std::array<wchar_t, 32'768> buffer{};
			const DWORD sdkLength = GetEnvironmentVariableW(
				L"VULKAN_SDK", buffer.data(), static_cast<DWORD>(buffer.size()));
			if (sdkLength > 0 && sdkLength < buffer.size())
			{
				const std::filesystem::path sdkRoot =
					std::filesystem::path(buffer.data()).lexically_normal();
				info.m_SdkVersion = sdkRoot.filename().wstring();
				const std::filesystem::path fromSdk = sdkRoot / L"Bin" / L"spirv-val.exe";
				std::error_code errorCode;
				if (info.m_SdkVersion == VulkanSdkValidationBaseline &&
					std::filesystem::exists(fromSdk, errorCode) &&
					QuerySpirVValidatorIdentity(fromSdk, info.m_ToolIdentity))
				{
					info.m_Path = fromSdk;
				}
			}
			return info;
		}

		[[nodiscard]] bool ValidateSpirVBinary(
			const std::filesystem::path& validator, const std::filesystem::path& binary) noexcept
		{
			if (validator.empty() || binary.empty())
			{
				return false;
			}
			std::wstring commandLine = std::format(L"\"{}\" --target-env vulkan1.3 \"{}\"",
				validator.wstring(), binary.wstring());
			STARTUPINFOW startupInfo{
				.cb = sizeof(STARTUPINFOW),
			};
			PROCESS_INFORMATION processInfo{};
			if (!CreateProcessW(validator.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
				CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo))
			{
				return false;
			}

			const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 30'000);
			if (waitResult == WAIT_TIMEOUT)
			{
				TerminateProcess(processInfo.hProcess, ERROR_TIMEOUT);
				WaitForSingleObject(processInfo.hProcess, INFINITE);
			}
			DWORD exitCode = ERROR_GEN_FAILURE;
			GetExitCodeProcess(processInfo.hProcess, &exitCode);
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			return waitResult == WAIT_OBJECT_0 && exitCode == 0;
		}

		[[nodiscard]] bool ReplaceMetadataValue(const std::filesystem::path& path,
			std::string_view target, std::string_view replacement) noexcept
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return false;
			}
			std::string content((std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			const size_t begin = content.find(target);
			if (begin == std::string::npos)
			{
				return false;
			}
			content.replace(begin, target.size(), replacement);
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(content.data(), static_cast<std::streamsize>(content.size()));
			return output.good();
		}

		void RunShaderBindingABITests(SelfTestContext& context) noexcept
		{
			context.Check(GGLabVulkanShaderRuntimeABI.m_Revision == 1,
				"Vulkan shader binding ABI revision is fixed at 1");

			struct FixedRegisterClassCase
			{
				VulkanShaderRegisterClass m_RegisterClass;
				VulkanFixedRegisterRange m_ExpectedRange;
			};
			constexpr std::array FixedRegisterClassCases{
				FixedRegisterClassCase{ VulkanShaderRegisterClass::ConstantBuffer, { 0, 32 } },
				FixedRegisterClassCase{ VulkanShaderRegisterClass::ShaderResource, { 32, 32 } },
				FixedRegisterClassCase{ VulkanShaderRegisterClass::UnorderedAccess, { 64, 32 } },
				FixedRegisterClassCase{ VulkanShaderRegisterClass::Sampler, { 96, 32 } },
			};
			bool rangesMatch = true;
			bool allBindingsUnique = true;
			bool allOutOfRangeIndicesRejected = true;
			std::array<bool, 128> occupiedBindings{};
			for (const auto& testCase : FixedRegisterClassCases)
			{
				const VulkanFixedRegisterRange range =
					GetVulkanFixedRegisterRange(testCase.m_RegisterClass);
				rangesMatch &= range.m_BindingShift == testCase.m_ExpectedRange.m_BindingShift &&
					range.m_RegisterCount == testCase.m_ExpectedRange.m_RegisterCount;
				for (uint32_t registerIndex = 0; registerIndex < range.m_RegisterCount;
					++registerIndex)
				{
					const auto result = EvaluateVulkanFixedShaderBinding(testCase.m_RegisterClass,
						registerIndex, GGLabVulkanShaderRuntimeABI.m_FixedHlslRegisterSpace);
					const uint32_t expectedBinding = range.m_BindingShift + registerIndex;
					allBindingsUnique &= result.IsSupported() &&
						result.m_Location.m_DescriptorSet ==
						GGLabVulkanShaderRuntimeABI.m_FixedDescriptorSet &&
						result.m_Location.m_Binding == expectedBinding &&
						expectedBinding < occupiedBindings.size() && !occupiedBindings[expectedBinding];
					if (expectedBinding < occupiedBindings.size())
					{
						occupiedBindings[expectedBinding] = true;
					}
				}
				const auto outOfRange = EvaluateVulkanFixedShaderBinding(testCase.m_RegisterClass,
					range.m_RegisterCount, GGLabVulkanShaderRuntimeABI.m_FixedHlslRegisterSpace);
				allOutOfRangeIndicesRejected &= !outOfRange.IsSupported() &&
					outOfRange.m_RejectionReason ==
					VulkanShaderBindingRejectionReason::FixedRegisterIndexOutOfRange;
			}
			context.Check(rangesMatch,
				"Vulkan fixed-register ranges lock 32 bindings at shifts 0, 32, 64, and 96");
			context.Check(allBindingsUnique && std::ranges::all_of(occupiedBindings,
				[](bool occupied) noexcept { return occupied; }),
				"Every fixed Vulkan binding is accepted exactly once without collisions");
			context.Check(allOutOfRangeIndicesRejected,
				"Every Vulkan fixed-register class rejects index 32 as out of range");

			const auto reservedSpace = EvaluateVulkanFixedShaderBinding(
				VulkanShaderRegisterClass::ShaderResource, 0,
				GGLabVulkanShaderRuntimeABI.m_GlobalHeapHlslRegisterSpace);
			context.Check(!reservedSpace.IsSupported() && reservedSpace.m_RejectionReason ==
				VulkanShaderBindingRejectionReason::ReservedGlobalHeapRegisterSpace,
				"Fixed bindings reject HLSL space1 reserved for global heaps");
			const auto unsupportedSpace = EvaluateVulkanFixedShaderBinding(
				VulkanShaderRegisterClass::ShaderResource, 0,
				GGLabVulkanShaderRuntimeABI.m_GlobalHeapHlslRegisterSpace + 1);
			context.Check(!unsupportedSpace.IsSupported() && unsupportedSpace.m_RejectionReason ==
				VulkanShaderBindingRejectionReason::UnsupportedFixedRegisterSpace,
				"Fixed bindings reject unsupported HLSL register spaces explicitly");

			const auto sampledTexture =
				EvaluateVulkanBindlessShaderBinding(VulkanBindlessResourceClass::SampledTexture);
			const auto storageTexture =
				EvaluateVulkanBindlessShaderBinding(VulkanBindlessResourceClass::StorageTexture);
			const auto sampler =
				EvaluateVulkanBindlessShaderBinding(VulkanBindlessResourceClass::Sampler);
			context.Check(sampledTexture.IsSupported() && storageTexture.IsSupported() &&
				sampledTexture.m_Location.m_DescriptorSet == 1 &&
				sampledTexture.m_Location.m_Binding == 0 &&
				storageTexture.m_Location.m_DescriptorSet == 1 &&
				storageTexture.m_Location.m_Binding == 0,
				"Sampled and storage textures share Vulkan set 1 binding 0");
			context.Check(sampler.IsSupported() && sampler.m_Location.m_DescriptorSet == 1 &&
				sampler.m_Location.m_Binding == 1,
				"Bindless samplers use Vulkan set 1 binding 1");

			const auto& mutableTypes =
				GGLabVulkanShaderBindingABI.m_ResourceHeapMutableAllowedTypes;
			context.Check(mutableTypes.size() == 2 &&
				mutableTypes[0] == VulkanDescriptorType::SampledImage &&
				mutableTypes[1] == VulkanDescriptorType::StorageImage,
				"Vulkan mutable resource binding allows exactly sampled and storage images");
			context.Check(GGLabVulkanShaderBindingABI.m_ResourceHeapDescriptorType ==
				VulkanDescriptorType::Mutable &&
				GGLabVulkanShaderBindingABI.m_SamplerHeapDescriptorType ==
				VulkanDescriptorType::Sampler,
				"Vulkan global heap descriptor types match binding ABI revision 1");
			context.Check(VulkanDescriptorTypeName(mutableTypes[0]) ==
				"VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE" &&
				VulkanDescriptorTypeName(mutableTypes[1]) ==
				"VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",
				"Vulkan mutable resource binding locks the exact native descriptor types");
			context.Check(GGLabVulkanShaderBindingABI.m_PartiallyBound &&
				GGLabVulkanShaderBindingABI.m_UpdateAfterBind &&
				GGLabVulkanShaderBindingABI.m_UpdateUnusedWhilePending,
				"Vulkan global heaps lock the descriptor publication binding flags");

			constexpr std::array UnsupportedResourceClasses{
				VulkanBindlessResourceClass::ConstantBuffer,
				VulkanBindlessResourceClass::ReadOnlyStorageBuffer,
				VulkanBindlessResourceClass::ReadWriteStorageBuffer,
				VulkanBindlessResourceClass::UniformTexelBuffer,
				VulkanBindlessResourceClass::StorageTexelBuffer,
				VulkanBindlessResourceClass::CombinedImageSampler,
				VulkanBindlessResourceClass::AccelerationStructure,
			};
			bool unsupportedClassesRejected = true;
			for (VulkanBindlessResourceClass resourceClass : UnsupportedResourceClasses)
			{
				const auto result = EvaluateVulkanBindlessShaderBinding(resourceClass);
				unsupportedClassesRejected &= !result.IsSupported() && result.m_RejectionReason ==
					VulkanShaderBindingRejectionReason::UnsupportedBindlessResourceClass;
			}
			context.Check(unsupportedClassesRejected,
				"Vulkan binding ABI revision 1 explicitly rejects non-image bindless resources");
		}

		void RunCoordinatePolicyTests(SelfTestContext& context) noexcept
		{
			context.Check(GGLabCoordinatePolicy.m_ClipDepthRange == RHIClipDepthRange::ZeroToOne &&
				GGLabCoordinatePolicy.m_ViewportOrigin == RHICoordinateOrigin::UpperLeft &&
				GGLabCoordinatePolicy.m_TextureUVOrigin == RHICoordinateOrigin::UpperLeft,
				"RHI coordinate policy uses zero-to-one depth and upper-left origins");
			context.Check(GGLabCoordinatePolicy.m_FrontFaceDefinition ==
				RHIFrontFaceDefinition::AfterViewportTransform &&
				!GGLabCoordinatePolicy.m_BackendAppliesReversedZ,
				"RHI front face is post-viewport and reversed-Z is not backend-added");
			context.Check(GGLabVulkanShaderRuntimeABI.m_InvertVertexProducingStageY &&
				GGLabVulkanShaderRuntimeABI.m_UseDxPositionW &&
				GGLabVulkanCoordinatePolicy.m_UsePositiveViewportHeight &&
				!GGLabVulkanCoordinatePolicy.m_BackendAppliesAdditionalReversedZ,
				"Vulkan coordinate lowering applies each required correction exactly once");
		}

		void RunShaderArtifactContractTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode) /
				std::format("GGLabVulkanShaderContracts-{}", GetCurrentProcessId());
			context.Check(!errorCode, "Vulkan shader contract test resolves a temporary cache root");
			if (errorCode)
			{
				return;
			}

			ScopedTestDirectory scopedDirectory(tempRoot);
			const std::filesystem::path shaderSourceRoot =
				ResolveShaderSourceRoot(win32::GetExecutableDirectory());
			const std::filesystem::path shaderCacheRoot = scopedDirectory.GetPath();
			ShaderCompiler compiler(shaderSourceRoot, shaderCacheRoot);
			const SpirVValidatorInfo validator = FindSpirVValidator();
			context.Check(validator.MatchesValidationBaseline(),
				"Vulkan SDK and SPIR-V Tools match the configured validation baseline");

			ShaderDesc dxilDesc{
				.m_SourcePath = L"Passes/PassForwardCoverage.hlsl",
				.m_Stage = ShaderStage::Vertex,
				.m_Entry = L"VSMain",
				.m_IncludeDirs = {L"."},
			};
			const ShaderResolvedRecipe dxilRecipe = compiler.Resolve(dxilDesc);
			const ShaderCompileResult dxilResult = compiler.CompileOrLoad(dxilRecipe);

			ShaderDesc spirVDesc = dxilDesc;
			spirVDesc.m_Target = MakeVulkan13CompileTarget(spirVDesc.m_Stage);
			const ShaderResolvedRecipe spirVRecipe = compiler.Resolve(spirVDesc);
			const ShaderCompileResult spirVResult = compiler.CompileOrLoad(spirVRecipe);
			const ShaderCompileResult cachedSpirVResult = compiler.CompileOrLoad(spirVRecipe);

			const std::filesystem::path dxilBinaryPath = compiler.GetCacheBinaryPath(dxilRecipe);
			const std::filesystem::path spirVBinaryPath = compiler.GetCacheBinaryPath(spirVRecipe);
			context.Check(dxilRecipe.IsSuccess() && spirVRecipe.IsSuccess() &&
				dxilResult.IsSuccess() && spirVResult.IsSuccess() &&
				dxilResult.m_Artifact.GetBinaryFormat() == ShaderBinaryFormat::Dxil &&
				spirVResult.m_Artifact.GetBinaryFormat() == ShaderBinaryFormat::SpirV,
				"One HLSL recipe produces valid DXIL and SPIR-V artifacts");
			context.Check(dxilBinaryPath.generic_wstring().find(L"/dxil/") != std::wstring::npos &&
				spirVBinaryPath.generic_wstring().find(L"/spirv/") != std::wstring::npos &&
				dxilBinaryPath.extension() == L".dxil" &&
				spirVBinaryPath.extension() == L".spv" &&
				dxilBinaryPath != spirVBinaryPath,
				"Shader cache partitions DXIL and SPIR-V by directory and extension");
			context.Check(!spirVResult.m_FromCache && cachedSpirVResult.m_FromCache,
				"SPIR-V artifact cache reuses an exact resolved recipe");

			const bool cacheBlobOverwritten =
				OverwriteBinaryFile(spirVBinaryPath, dxilResult.m_Artifact.m_Binary);
			const ShaderCompileResult recoveredSpirVResult = compiler.CompileOrLoad(spirVRecipe);
			SpirVDecorationReflection recoveredReflection;
			context.Check(cacheBlobOverwritten && !recoveredSpirVResult.m_FromCache &&
				ReadSpirVDecorations(recoveredSpirVResult.m_Artifact.m_Binary, recoveredReflection),
				"Shader cache rejects a blob whose actual format disagrees with its target metadata");
			context.Check(!compiler.GetCompilerIdentity().m_CanonicalIdentity.empty() &&
				compiler.GetCompilerIdentity().m_CanonicalIdentity != L"unknown",
				"Active shader compiler exposes the concrete DXC producer identity");

			const std::filesystem::path runtimeArtifactRoot =
				scopedDirectory.GetPath() / "RuntimeArtifacts";
			const ShaderRuntimeArtifactPublicationResult dxilPublication =
				PublishShaderRuntimeArtifact(runtimeArtifactRoot, dxilResult.m_Artifact);
			const ShaderRuntimeArtifactPublicationResult spirVPublication =
				PublishShaderRuntimeArtifact(runtimeArtifactRoot, spirVResult.m_Artifact);
			const std::array registryEntries{
				ShaderProgramRegistryEntry{
					.m_ProgramRef = shader_programs::ForwardCoverageVertex,
					.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
					.m_ArtifactRef = dxilPublication.m_ArtifactRef,
				},
				ShaderProgramRegistryEntry{
					.m_ProgramRef = shader_programs::ForwardCoverageVertex,
					.m_TargetProfile = ShaderTargetProfile::GGLabVulkan13,
					.m_ArtifactRef = spirVPublication.m_ArtifactRef,
				},
			};
			const ShaderProgramRegistryArtifactBuildResult registryBuild =
				BuildShaderProgramRegistryArtifact(registryEntries);
			const ShaderProgramRegistryArtifactPublicationResult registryPublication =
				registryBuild.IsSuccess()
					? PublishShaderProgramRegistryArtifact(
						runtimeArtifactRoot, registryBuild.m_Artifact)
					: ShaderProgramRegistryArtifactPublicationResult{};
			ShaderManager dxilManager({
				.m_ActiveBackend = RHIBackendType::DX12,
				.m_ArtifactRoot = runtimeArtifactRoot,
				.m_ActiveRegistry = registryPublication.m_RegistryRef,
				});
			const ShaderID dxilManagerShader =
				dxilManager.LoadProgram(shader_programs::ForwardCoverageVertex);
			ShaderManager spirVManager({
				.m_ActiveBackend = RHIBackendType::Vulkan,
				.m_ArtifactRoot = runtimeArtifactRoot,
				.m_ActiveRegistry = registryPublication.m_RegistryRef,
				});
			const ShaderID spirVManagerShader =
				spirVManager.LoadProgram(shader_programs::ForwardCoverageVertex);
			context.Check(dxilPublication.IsSuccess() && spirVPublication.IsSuccess() &&
				registryPublication.IsSuccess() && dxilManager.IsReady() &&
				spirVManager.IsReady() &&
				dxilManager.GetActiveBackend() == RHIBackendType::DX12 &&
				spirVManager.GetActiveBackend() == RHIBackendType::Vulkan &&
				dxilManagerShader.IsValid() && spirVManagerShader.IsValid() &&
				dxilManager.GetBytecode(dxilManagerShader).m_Format == ShaderBinaryFormat::Dxil &&
				spirVManager.GetBytecode(spirVManagerShader).m_Format == ShaderBinaryFormat::SpirV &&
				dxilManager.GetBytecode(dxilManagerShader).m_EntryPoint == "VSMain" &&
				spirVManager.GetBytecode(spirVManagerShader).m_EntryPoint == "VSMain" &&
				shader_programs::ForwardCoverageVertex.m_Stage == ShaderStage::Vertex &&
				dxilManager.ResolveArtifact(shader_programs::ForwardCoverageVertex).has_value() &&
				spirVManager.ResolveArtifact(shader_programs::ForwardCoverageVertex).has_value() &&
				dxilManager.ResolveArtifact(shader_programs::ForwardCoverageVertex) !=
					spirVManager.ResolveArtifact(shader_programs::ForwardCoverageVertex),
				"ShaderManager loads backend artifacts from an injected immutable registry");

			// Recipe identity semantics: the resolved recipe is the authority, and
			// producer identity is a separate build-key axis.
			const ShaderCompilerIdentity compilerIdentity = compiler.GetCompilerIdentity();
			ShaderCompilerIdentity differentIdentity = compilerIdentity;
			differentIdentity.m_CanonicalIdentity += L"-different";
			ShaderDesc changedArgumentsDesc = spirVDesc;
			changedArgumentsDesc.m_ExtraArgs.push_back(L"-GGLAB_TEST_ARGUMENT");
			const ShaderResolvedRecipe changedArgumentsRecipe = compiler.Resolve(changedArgumentsDesc);
			ShaderDesc canonicalizedCoordinatesDesc = spirVDesc;
			canonicalizedCoordinatesDesc.m_Target.m_CoordinateOptions =
				ShaderCoordinateOptions::UseDxPositionW;
			const ShaderResolvedRecipe canonicalizedCoordinatesRecipe =
				compiler.Resolve(canonicalizedCoordinatesDesc);
			context.Check(dxilRecipe.m_RecipeId != spirVRecipe.m_RecipeId &&
				spirVRecipe.m_RecipeId != changedArgumentsRecipe.m_RecipeId &&
				spirVRecipe.m_RecipeId == canonicalizedCoordinatesRecipe.m_RecipeId &&
				spirVRecipe.m_RecipeId.m_DurableDigest != spirVRecipe.m_BuildKey.m_DurableDigest,
				"Resolved recipe identity covers the normalized logical request and canonicalizes caller coordinate expressions");
			context.Check(ShaderCompiler::ComputeBuildKey(spirVRecipe.m_RecipeId, compilerIdentity) ==
				spirVRecipe.m_BuildKey &&
				ShaderCompiler::ComputeBuildKey(spirVRecipe.m_RecipeId, differentIdentity) !=
				spirVRecipe.m_BuildKey,
				"Producer identity participates in the build key, not the recipe identity");

			// Logical source identity: absolute paths are rejected, and the
			// recipe identity is the logical path, not the physical checkout path.
			ShaderDesc absoluteDesc = spirVDesc;
			absoluteDesc.m_SourcePath = spirVRecipe.m_Request.m_SourcePath;
			const ShaderResolvedRecipe absoluteRecipe = compiler.Resolve(absoluteDesc);
			context.Check(!absoluteRecipe.IsSuccess() &&
				absoluteRecipe.m_Status == ShaderCompileStatus::InvalidRequest,
				"Resolve rejects absolute source paths outside the logical identity contract");

			constexpr std::array ReservedArguments{
				L"-spirv",
				L"-fspv-target-env=vulkan1.0",
				L"-fvk-invert-y",
				L"-T",
				L"-O0",
				L"@shader-options.rsp",
			};
			bool allReservedArgumentsRejected = true;
			for (std::wstring_view argument : ReservedArguments)
			{
				ShaderDesc bypassDesc = spirVDesc;
				bypassDesc.m_ExtraArgs = { std::wstring(argument) };
				const ShaderResolvedRecipe recipe = compiler.Resolve(bypassDesc);
				allReservedArgumentsRejected &= !recipe.IsSuccess() &&
					recipe.m_Status == ShaderCompileStatus::InvalidRequest &&
					recipe.m_Diagnostics.m_ValidationError ==
					ShaderCompileValidationError::ReservedExtraArgument;
			}
			context.Check(allReservedArgumentsRejected,
				"Shader validation prevents extra arguments from overriding normalized target options");

			// An explicit illegal binding ABI revision must be reported, not
			// silently corrected during normalization.
			ShaderDesc mismatchedAbiDesc = spirVDesc;
			mismatchedAbiDesc.m_Target.m_BindingABIRevision = 999;
			const ShaderResolvedRecipe mismatchedAbiRecipe = compiler.Resolve(mismatchedAbiDesc);
			context.Check(!mismatchedAbiRecipe.IsSuccess() &&
				mismatchedAbiRecipe.m_Status == ShaderCompileStatus::InvalidRequest &&
				mismatchedAbiRecipe.m_Diagnostics.m_ValidationError ==
				ShaderCompileValidationError::UnsupportedBindingABIRevision,
				"Resolve reports an explicit illegal binding ABI revision instead of correcting it");

			// DXC unavailable maps to CompilerUnavailable without aborting the process.
			std::unique_ptr<ShaderCompiler> unavailableCompiler =
				ShaderCompiler::MakeUnavailable(shaderSourceRoot, shaderCacheRoot);
			const ShaderResolvedRecipe unavailableRecipe = unavailableCompiler->Resolve(spirVDesc);
			const ShaderCompileResult unavailableResult = unavailableCompiler->Compile(spirVDesc);
			context.Check(!unavailableRecipe.IsSuccess() &&
				unavailableRecipe.m_Status == ShaderCompileStatus::CompilerUnavailable &&
				!unavailableResult.IsSuccess() &&
				unavailableResult.m_Status == ShaderCompileStatus::CompilerUnavailable,
				"DXC initialization failure maps to CompilerUnavailable without a constructor abort");

			ShaderDesc rejectedCompileDesc = spirVDesc;
			rejectedCompileDesc.m_ExtraArgs = { L"-fspv-target-env=vulkan1.0" };
			const ShaderCompileResult rejectedCompileResult = compiler.Compile(rejectedCompileDesc);
			context.Check(!rejectedCompileResult.IsSuccess() &&
				rejectedCompileResult.m_Status == ShaderCompileStatus::InvalidRequest &&
				rejectedCompileResult.m_Diagnostics.m_ValidationError ==
				ShaderCompileValidationError::ReservedExtraArgument,
				"Shader compiler rejects invalid target contracts without relying on assertions");

			ShaderDesc missingDesc = spirVDesc;
			missingDesc.m_SourcePath = L"Passes/PassDoesNotExist.hlsl";
			const ShaderCompileResult missingResult = compiler.Compile(missingDesc);
			context.Check(!missingResult.IsSuccess() &&
				missingResult.m_Status == ShaderCompileStatus::SourceNotFound,
				"Missing shader source maps to SourceNotFound instead of a fatal invariant");

			// DXC syntax errors are recoverable CompileFailed outcomes carrying the
			// raw DXC message.
			const std::filesystem::path badSourceRoot = tempRoot / L"BadShaderSources";
			const std::filesystem::path badCacheRoot = tempRoot / L"BadShaderCache";
			std::filesystem::create_directories(badSourceRoot);
			const bool badSourceWritten =
				OverwriteTextFile(badSourceRoot / L"Bad.hlsl", "this is not valid hlsl");
			ShaderCompiler badCompiler(badSourceRoot, badCacheRoot);
			ShaderDesc badDesc{
				.m_SourcePath = L"Bad.hlsl",
				.m_Stage = ShaderStage::Compute,
				.m_Entry = L"CSMain",
			};
			const ShaderCompileResult badResult = badCompiler.Compile(badDesc);
			context.Check(badSourceWritten && !badResult.IsSuccess() &&
				badResult.m_Status == ShaderCompileStatus::CompileFailed &&
				!badResult.m_Diagnostics.m_Message.empty(),
				"DXC syntax errors map to CompileFailed with the raw DXC diagnostics");

			// A corrupted cached binary is a cache miss, never a fatal read.
			const bool cacheCorrupted =
				OverwriteTextFile(spirVBinaryPath, "corrupted derived data");
			const ShaderCompileResult corruptedRecoveryResult = compiler.CompileOrLoad(spirVRecipe);
			context.Check(cacheCorrupted && corruptedRecoveryResult.IsSuccess() &&
				!corruptedRecoveryResult.m_FromCache,
				"Corrupt cached binary data is treated as a cache miss and rebuilt");

			// Resolved recipe identity is deterministic across instances, and a
			// second instance reuses the shared cache entry.
			ShaderCompiler secondCompiler(shaderSourceRoot, shaderCacheRoot);
			const ShaderResolvedRecipe secondRecipe = secondCompiler.Resolve(spirVDesc);
			const ShaderCompileResult secondResult = secondCompiler.CompileOrLoad(secondRecipe);
			context.Check(secondRecipe.m_RecipeId == spirVRecipe.m_RecipeId &&
				secondRecipe.m_BuildKey == spirVRecipe.m_BuildKey &&
				secondResult.IsSuccess() && secondResult.m_FromCache,
				"A second compiler instance resolves the same identity and reuses the shared cache");

			// Two instances publishing the same recipe concurrently converge on one
			// valid entry; neither consumes a partial publication. Each worker's
			// full compile result is captured so an intermittent failure carries
			// the structured diagnostics that localize the failing publication
			// branch. Rounds use distinct recipes so every round races a cold
			// cache slot instead of reusing the previous winner.
			constexpr int ConcurrentPublicationRoundCount = 3;
			bool allConcurrentWorkersSucceeded = true;
			bool allConcurrentReloadsValid = true;
			bool allConcurrentReloadsHitCache = true;
			std::string concurrentWorkerDiagnostics;
			for (int round = 0; round < ConcurrentPublicationRoundCount; ++round)
			{
				ShaderDesc roundDesc = spirVDesc;
				roundDesc.m_ExtraArgs = {
					std::format(L"-DGGLAB_CONCURRENT_PUBLICATION={}", round),
				};

				ShaderCompileResult firstWorkerResult{};
				ShaderCompileResult secondWorkerResult{};
				std::thread firstConcurrent([&]() noexcept
					{
						ShaderCompiler worker(shaderSourceRoot, shaderCacheRoot);
						firstWorkerResult = worker.Compile(roundDesc);
					});
				std::thread secondConcurrent([&]() noexcept
					{
						ShaderCompiler worker(shaderSourceRoot, shaderCacheRoot);
						secondWorkerResult = worker.Compile(roundDesc);
					});
				firstConcurrent.join();
				secondConcurrent.join();

				const ShaderResolvedRecipe roundRecipe = compiler.Resolve(roundDesc);
				const ShaderCompileResult roundReload = compiler.CompileOrLoad(roundRecipe);

				allConcurrentWorkersSucceeded &=
					firstWorkerResult.IsSuccess() && secondWorkerResult.IsSuccess();
				allConcurrentReloadsValid &= roundReload.IsSuccess();
				allConcurrentReloadsHitCache &= roundReload.m_FromCache;
				if (!firstWorkerResult.IsSuccess() || !secondWorkerResult.IsSuccess())
				{
					const auto AppendWorkerDiagnostics = [&concurrentWorkerDiagnostics](
						int round, std::string_view tag,
						const ShaderCompileResult& result) noexcept
						{
							concurrentWorkerDiagnostics += std::format(
								" [round{}-{}: status={} validationError={} message=\"{}\" sourceIdentity=\"{}\"]",
								round, tag, static_cast<int>(result.m_Status),
								static_cast<int>(result.m_Diagnostics.m_ValidationError),
								utils::ToString(result.m_Diagnostics.m_Message),
								utils::ToString(result.m_Diagnostics.m_SourceIdentity));
						};
					if (!firstWorkerResult.IsSuccess())
					{
						AppendWorkerDiagnostics(round, "A", firstWorkerResult);
					}
					if (!secondWorkerResult.IsSuccess())
					{
						AppendWorkerDiagnostics(round, "B", secondWorkerResult);
					}
				}
			}
			context.Check(allConcurrentWorkersSucceeded,
				("Both concurrent compiler instances complete their publication"
					+ concurrentWorkerDiagnostics).c_str());
			context.Check(allConcurrentReloadsValid,
				"Reload after concurrent publication yields a valid artifact");
			context.Check(allConcurrentReloadsHitCache,
				"Reload after concurrent publication hits the committed cache entry");

			const std::vector<std::wstring> vertexArguments = spirVRecipe.m_CompileArguments;
			bool registerShiftsMatch = true;
			const std::array registerShiftOptions{
				std::pair{ L"-fvk-b-shift", VulkanShaderRegisterClass::ConstantBuffer },
				std::pair{ L"-fvk-t-shift", VulkanShaderRegisterClass::ShaderResource },
				std::pair{ L"-fvk-u-shift", VulkanShaderRegisterClass::UnorderedAccess },
				std::pair{ L"-fvk-s-shift", VulkanShaderRegisterClass::Sampler },
			};
			for (const auto& [option, registerClass] : registerShiftOptions)
			{
				const VulkanFixedRegisterRange range = GetVulkanFixedRegisterRange(registerClass);
				const std::wstring shift = std::to_wstring(range.m_BindingShift);
				const std::wstring hlslSpace =
					std::to_wstring(GGLabVulkanShaderRuntimeABI.m_FixedHlslRegisterSpace);
				registerShiftsMatch &=
					ContainsArgumentSequence(vertexArguments, { option, shift, hlslSpace });
			}
			const std::wstring resourceBinding =
				std::to_wstring(GGLabVulkanShaderRuntimeABI.m_ResourceHeapBinding);
			const std::wstring samplerBinding =
				std::to_wstring(GGLabVulkanShaderRuntimeABI.m_SamplerHeapBinding);
			const std::wstring descriptorSet =
				std::to_wstring(GGLabVulkanShaderRuntimeABI.m_GlobalDescriptorSet);
			context.Check(registerShiftsMatch && ContainsArgumentSequence(vertexArguments,
				{ L"-fvk-bind-resource-heap", resourceBinding, descriptorSet }) &&
				ContainsArgumentSequence(vertexArguments,
					{ L"-fvk-bind-sampler-heap", samplerBinding, descriptorSet }),
				"SPIR-V compile arguments consume the centralized HLSL-space and descriptor-set ABI");
			context.Check(ContainsArgument(vertexArguments, L"-spirv") &&
				ContainsArgument(vertexArguments, L"-fspv-target-env=vulkan1.3") &&
				ContainsArgument(vertexArguments, L"-fvk-use-dx-layout") &&
				ContainsArgument(vertexArguments, L"-fvk-invert-y") &&
				!ContainsArgument(vertexArguments, L"-fvk-use-dx-position-w"),
				"Vertex SPIR-V compile policy targets Vulkan 1.3 with DX layout and one Y inversion");

			ShaderDesc forwardPixelDesc{
				.m_SourcePath = L"Passes/PassForwardPBR.hlsl",
				.m_Stage = ShaderStage::Pixel,
				.m_Target = MakeVulkan13CompileTarget(ShaderStage::Pixel),
				.m_Entry = L"PSMain",
				.m_IncludeDirs = {L"."},
			};
			const ShaderResolvedRecipe forwardPixelRecipe = compiler.Resolve(forwardPixelDesc);
			context.Check(ContainsArgument(forwardPixelRecipe.m_CompileArguments,
				L"-fvk-use-dx-position-w") &&
				!ContainsArgument(forwardPixelRecipe.m_CompileArguments, L"-fvk-invert-y"),
				"Pixel SPIR-V compile policy preserves the HLSL SV_Position.w contract");

			const std::filesystem::path spirVRecordPath = MakeCacheRecordPath(spirVBinaryPath);
			std::string metadata;
			{
				std::ifstream metadataInput(spirVRecordPath, std::ios::binary);
				metadata.assign((std::istreambuf_iterator<char>(metadataInput)),
					std::istreambuf_iterator<char>());
			}
			// The read handle is closed before CompileOrLoad below: the
			// publication protocol commits the cache record by replacing it, and a
			// destination held open would fail that rename, classify the stale
			// entry as committed-by-other, and skip the rebuild.
			context.Check(metadata.find("\"recordSchemaVersion\":2") != std::string::npos &&
				metadata.find("\"schemaVersion\":2") != std::string::npos &&
				metadata.find("\"recipeHashSchema\":1") != std::string::npos &&
				metadata.find("\"recipeId\":\"") != std::string::npos &&
				metadata.find("\"buildKey\":\"") != std::string::npos &&
				metadata.find("\"binaryContentDigest\":\"") != std::string::npos &&
				metadata.find("\"targetProfile\":\"gglab-vulkan13\"") != std::string::npos &&
				metadata.find("\"binaryFormat\":\"spirv\"") != std::string::npos &&
				metadata.find("\"spirvTargetEnvironment\":\"vulkan1.3\"") != std::string::npos &&
				metadata.find("\"bindingAbiRevision\":1") != std::string::npos &&
				metadata.find("\"shaderModel\":\"6_7\"") != std::string::npos &&
				metadata.find("\"hlslVersion\":\"2021\"") != std::string::npos &&
				metadata.find("\"compileFlags\":") != std::string::npos &&
				metadata.find("\"optimizationLevel\":") != std::string::npos &&
				metadata.find("\"logicalIncludeDirs\":") != std::string::npos &&
				metadata.find("\"identity\":\"") != std::string::npos &&
				metadata.find("\"logicalSource\":\"Passes/PassForwardCoverage.hlsl\"") != std::string::npos &&
				metadata.find("\"local\":{") != std::string::npos &&
				metadata.find("\"physicalSource\":\"") != std::string::npos &&
				metadata.find("\"physicalIncludeDirs\":") != std::string::npos &&
				metadata.find("\"dependencies\":[") != std::string::npos &&
				metadata.find("\"contentDigest\":\"") != std::string::npos &&
				metadata.find("\"dependencyPhysicalPaths\":[") != std::string::npos,
				"Cache record carries the complete portable manifest (profile, target semantics, logical identities, dependency provenance) plus explicit local validation state");
			const bool metadataChanged = ReplaceMetadataValue(
				spirVRecordPath, "\"bindingAbiRevision\":1", "\"bindingAbiRevision\":999");
			const ShaderCompileResult rejectedCacheArtifact = compiler.CompileOrLoad(spirVRecipe);
			const std::optional<ShaderArtifactCacheRecord> repairedReadBack =
				ReadShaderArtifactCacheRecord(spirVRecordPath);
			context.Check(metadataChanged,
				"Shader cache metadata tamper is applied");
			context.Check(repairedReadBack.has_value() &&
				repairedReadBack->m_Manifest.m_BindingABIRevision == 1,
				"Rejected tampered entry is rebuilt and republished with the supported ABI revision");
			context.Check(metadataChanged && rejectedCacheArtifact.IsSuccess() &&
				!rejectedCacheArtifact.m_FromCache,
				"Shader cache rejects a manifest tampered with an unsupported binding ABI revision and rebuilds it");

			// A missing required durable field must also reject the entry.
			const bool manifestFieldRemoved = ReplaceMetadataValue(
				spirVRecordPath, "\"hlslVersion\":\"2021\",", "");
			const ShaderCompileResult missingFieldArtifact = compiler.CompileOrLoad(spirVRecipe);
			context.Check(manifestFieldRemoved && !missingFieldArtifact.m_FromCache,
				"Shader cache rejects a manifest missing a required durable field");

			SpirVDecorationReflection coverageReflection;
			const bool coverageReflected =
				ReadSpirVDecorations(spirVResult.m_Artifact.m_Binary, coverageReflection);
			const SpirVEntryPointReflection* coverageEntry =
				coverageReflection.FindEntryPoint("VSMain");
			const std::vector<uint32_t> expectedVertexLocations{ 0, 1, 2, 3, 4 };
			context.Check(coverageReflected && coverageEntry &&
				coverageEntry->m_ExecutionModel == SpirVExecutionModel::Vertex &&
				coverageEntry->m_InputLocations == expectedVertexLocations &&
				coverageEntry->m_OutputBuiltInCount > 0,
				"SPIR-V reader resolves vertex stage, user locations, and member BuiltIns");
			context.Check(HasDescriptorBinding(coverageReflection, 0, 2),
				"Forward coverage SPIR-V maps b2 to set 0 binding 2");

			const ShaderCompileResult forwardPixelResult = compiler.CompileOrLoad(forwardPixelRecipe);
			SpirVDecorationReflection forwardPixelReflection;
			const bool forwardPixelReflected =
				ReadSpirVDecorations(forwardPixelResult.m_Artifact.m_Binary, forwardPixelReflection);
			const SpirVEntryPointReflection* forwardPixelEntry =
				forwardPixelReflection.FindEntryPoint("PSMain");
			context.Check(forwardPixelReflected && forwardPixelEntry &&
				forwardPixelEntry->m_ExecutionModel == SpirVExecutionModel::Fragment &&
				forwardPixelEntry->m_InputBuiltInCount > 0 &&
				HasDescriptorBinding(forwardPixelReflection, 1, 0) &&
				HasDescriptorBinding(forwardPixelReflection, 1, 1),
				"Forward PBR SPIR-V exposes fragment stage and both global heap bindings");

			ShaderDesc cullDesc{
				.m_SourcePath = L"Passes/PassForwardPlusCull.hlsl",
				.m_Stage = ShaderStage::Compute,
				.m_Target = MakeVulkan13CompileTarget(ShaderStage::Compute),
				.m_Entry = L"CSMain",
				.m_IncludeDirs = {L"."},
			};
			const ShaderResolvedRecipe cullRecipe = compiler.Resolve(cullDesc);
			const ShaderCompileResult cullResult = compiler.CompileOrLoad(cullRecipe);
			SpirVDecorationReflection cullReflection;
			const bool cullReflected =
				ReadSpirVDecorations(cullResult.m_Artifact.m_Binary, cullReflection);
			const SpirVEntryPointReflection* cullEntry = cullReflection.FindEntryPoint("CSMain");
			const std::array cullFixedBindings{ 0u, 32u, 33u, 64u, 65u };
			const bool cullBindingsMatch = std::ranges::all_of(cullFixedBindings,
				[&cullReflection](uint32_t binding) noexcept
				{
					return HasDescriptorBinding(cullReflection, 0, binding);
				});
			context.Check(cullReflected && cullEntry &&
				cullEntry->m_ExecutionModel == SpirVExecutionModel::Compute &&
				cullBindingsMatch && HasDescriptorBinding(cullReflection, 1, 0),
				"Forward+ SPIR-V locks fixed CBV/SRV/UAV shifts and the global resource heap");

			ShaderDesc gtaoDesc{
				.m_SourcePath = L"Passes/PassGTAO.hlsl",
				.m_Stage = ShaderStage::Compute,
				.m_Target = MakeVulkan13CompileTarget(ShaderStage::Compute),
				.m_Entry = L"CSMain",
				.m_IncludeDirs = {L"."},
			};
			const ShaderResolvedRecipe gtaoRecipe = compiler.Resolve(gtaoDesc);
			const ShaderCompileResult gtaoResult = compiler.CompileOrLoad(gtaoRecipe);
			gtaoDesc.m_Defines = {
				{.m_Name = L"GGLAB_GTAO_DIAGNOSTICS", .m_Value = L"1"},
			};
			const ShaderResolvedRecipe gtaoDiagnosticsRecipe = compiler.Resolve(gtaoDesc);
			const ShaderCompileResult gtaoDiagnosticsResult =
				compiler.CompileOrLoad(gtaoDiagnosticsRecipe);

			ShaderDesc storageDesc{
				.m_SourcePath = L"Passes/PassRenderGraphComputeSmoke.hlsl",
				.m_Stage = ShaderStage::Compute,
				.m_Target = MakeVulkan13CompileTarget(ShaderStage::Compute),
				.m_Entry = L"CSWrite",
				.m_IncludeDirs = {L"."},
			};
			const ShaderResolvedRecipe storageRecipe = compiler.Resolve(storageDesc);
			const ShaderCompileResult storageResult = compiler.CompileOrLoad(storageRecipe);
			SpirVDecorationReflection storageReflection;
			const bool storageReflected =
				ReadSpirVDecorations(storageResult.m_Artifact.m_Binary, storageReflection);
			context.Check(storageReflected && HasDescriptorBinding(storageReflection, 0, 2) &&
				HasDescriptorBinding(storageReflection, 1, 0),
				"RenderGraph storage-write SPIR-V maps fixed constants and the mutable resource heap");

			ShaderDesc fullscreenDesc = storageDesc;
			fullscreenDesc.m_Stage = ShaderStage::Vertex;
			fullscreenDesc.m_Target = MakeVulkan13CompileTarget(ShaderStage::Vertex);
			fullscreenDesc.m_Entry = L"VSMain";
			const ShaderResolvedRecipe fullscreenRecipe = compiler.Resolve(fullscreenDesc);
			const ShaderCompileResult fullscreenResult = compiler.CompileOrLoad(fullscreenRecipe);
			SpirVDecorationReflection fullscreenReflection;
			const bool fullscreenReflected =
				ReadSpirVDecorations(fullscreenResult.m_Artifact.m_Binary, fullscreenReflection);
			const SpirVEntryPointReflection* fullscreenEntry =
				fullscreenReflection.FindEntryPoint("VSMain");
			context.Check(fullscreenReflected && fullscreenEntry &&
				fullscreenEntry->m_InputLocations.empty() &&
				fullscreenEntry->m_InputBuiltInCount > 0 &&
				fullscreenEntry->m_OutputBuiltInCount > 0,
				"SPIR-V reader excludes SV_VertexID and SV_Position from user locations");

			const std::array executionModelCases{
				std::pair{ 5267u, SpirVExecutionModel::Task },
				std::pair{ 5268u, SpirVExecutionModel::Mesh },
				std::pair{ 5364u, SpirVExecutionModel::Task },
				std::pair{ 5365u, SpirVExecutionModel::Mesh },
			};
			bool executionModelsRecognized = true;
			for (const auto& [model, expected] : executionModelCases)
			{
				SpirVDecorationReflection reflection;
				const ShaderBinary binary = MakeExecutionModelModule(model);
				executionModelsRecognized &= ReadSpirVDecorations(binary, reflection) &&
					reflection.FindEntryPoint("main") &&
					reflection.FindEntryPoint("main")->m_ExecutionModel == expected;
			}
			context.Check(executionModelsRecognized,
				"SPIR-V reader recognizes both EXT and NV task and mesh execution models");

			const std::array validationCases{
				std::pair{ &spirVResult, &spirVRecipe },
				std::pair{ &forwardPixelResult, &forwardPixelRecipe },
				std::pair{ &cullResult, &cullRecipe },
				std::pair{ &gtaoResult, &gtaoRecipe },
				std::pair{ &gtaoDiagnosticsResult, &gtaoDiagnosticsRecipe },
				std::pair{ &storageResult, &storageRecipe },
				std::pair{ &fullscreenResult, &fullscreenRecipe },
			};
			const bool allValidated = validator.MatchesValidationBaseline() &&
				std::ranges::all_of(validationCases,
					[&validator, &compiler](const auto& validationCase) noexcept
					{
						const ShaderCompileResult* result = validationCase.first;
						const ShaderResolvedRecipe* recipe = validationCase.second;
						return result->IsSuccess() &&
							ValidateSpirVBinary(validator.m_Path,
								compiler.GetCacheBinaryPath(*recipe));
					});
			context.Check(allValidated,
				"Baseline spirv-val accepts representative vertex, pixel, compute, GTAO, and storage artifacts");
		}

		[[nodiscard]] bool WriteTextFile(
			const std::filesystem::path& path, std::string_view content) noexcept
		{
			std::filesystem::create_directories(path.parent_path());
			std::ofstream output(path, std::ios::binary);
			output.write(content.data(), static_cast<std::streamsize>(content.size()));
			return output.good();
		}

		// Rewrites the first occurrence of a JSON integer field's value (used
		// to tamper with recorded cache record fields in contract tests).
		[[nodiscard]] bool OverwriteJsonIntegerField(
			const std::filesystem::path& path, std::string_view fieldName,
			std::string_view replacement) noexcept
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return false;
			}
			std::string content((std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			const std::string prefix = std::string("\"") + std::string(fieldName) + "\":";
			const std::size_t begin = content.find(prefix);
			if (begin == std::string::npos)
			{
				return false;
			}
			std::size_t numberBegin = begin + prefix.size();
			while (numberBegin < content.size() &&
				(content[numberBegin] == ' ' || content[numberBegin] == '\t'))
			{
				++numberBegin;
			}
			if (numberBegin >= content.size() ||
				(content[numberBegin] != '-' &&
					(content[numberBegin] < '0' || content[numberBegin] > '9')))
			{
				return false;
			}
			std::size_t numberEnd = numberBegin + 1;
			while (numberEnd < content.size() &&
				content[numberEnd] >= '0' && content[numberEnd] <= '9')
			{
				++numberEnd;
			}
			content.replace(numberBegin, numberEnd - numberBegin, replacement);
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(content.data(), static_cast<std::streamsize>(content.size()));
			return output.good();
		}

		void RunCrossCheckoutIdentityTests(SelfTestContext& context) noexcept
		{
			// The same logical request must produce the same recipe and build
			// identity from two different checkout locations. The include
			// configuration participates logically, never as a physical -I.
			std::error_code errorCode;
			const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode) /
				std::format("GGLabCrossCheckout-{}", GetCurrentProcessId());
			context.Check(!errorCode, "Cross-checkout test resolves a temporary root");
			if (errorCode)
			{
				return;
			}
			ScopedTestDirectory scopedDirectory(tempRoot);

			constexpr std::string_view IncludeContent = "#define GGLAB_CROSS_CHECKOUT 1\n";
			constexpr std::string_view SourceContent =
				"#include \"Common/CheckoutProbe.hlsli\"\n"
				"float4 VSMain(uint id : SV_VertexID) : SV_Position "
				"{ return float4(0, 0, 0, 1); }\n";
			const std::filesystem::path checkoutA = tempRoot / L"CheckoutA";
			const std::filesystem::path checkoutB = tempRoot / L"CheckoutB";
			const bool filesWritten =
				WriteTextFile(checkoutA / L"Common/CheckoutProbe.hlsli", IncludeContent) &&
				WriteTextFile(checkoutA / L"Probe.hlsl", SourceContent) &&
				WriteTextFile(checkoutB / L"Common/CheckoutProbe.hlsli", IncludeContent) &&
				WriteTextFile(checkoutB / L"Probe.hlsl", SourceContent);

			ShaderCompiler compilerA(checkoutA, tempRoot / L"CacheA");
			ShaderCompiler compilerB(checkoutB, tempRoot / L"CacheB");
			ShaderDesc descA{
				.m_SourcePath = L"Probe.hlsl",
				.m_Stage = ShaderStage::Vertex,
				.m_Entry = L"VSMain",
				.m_IncludeDirs = { L"." },
			};
			const ShaderResolvedRecipe recipeA = compilerA.Resolve(descA);
			const ShaderResolvedRecipe recipeB = compilerB.Resolve(descA);
			const ShaderCompileResult resultA = compilerA.CompileOrLoad(recipeA);
			const ShaderCompileResult resultB = compilerB.CompileOrLoad(recipeB);

			context.Check(filesWritten && recipeA.IsSuccess() && recipeB.IsSuccess() &&
				resultA.IsSuccess() && resultB.IsSuccess() &&
				recipeA.m_RecipeId == recipeB.m_RecipeId &&
				recipeA.m_BuildKey == recipeB.m_BuildKey &&
				resultA.m_Artifact.m_Manifest.m_BinaryContentDigest ==
				resultB.m_Artifact.m_Manifest.m_BinaryContentDigest,
				"Same logical request resolves to the same recipe/build identity across checkout locations");

			// The recipe identity must not embed physical -I arguments: a
			// source-root-relative include configuration and its logical form
			// must agree even though the physical paths differ.
			ShaderDesc absoluteIncludeDesc = descA;
			absoluteIncludeDesc.m_IncludeDirs = { checkoutA };
			const ShaderResolvedRecipe absoluteIncludeRecipe =
				compilerA.Resolve(absoluteIncludeDesc);
			context.Check(absoluteIncludeRecipe.IsSuccess() &&
				recipeA.m_RecipeId == absoluteIncludeRecipe.m_RecipeId,
				"Absolute include dirs under the source root canonicalize to the same logical identity");
		}

		void RunRecipeAuthorityTests(SelfTestContext& context) noexcept
		{
			const std::filesystem::path shaderSourceRoot =
				ResolveShaderSourceRoot(win32::GetExecutableDirectory());
			std::error_code errorCode;
			const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode) /
				std::format("GGLabRecipeAuthority-{}", GetCurrentProcessId());
			ScopedTestDirectory scopedDirectory(tempRoot);
			ShaderCompiler compiler(shaderSourceRoot, tempRoot / L"AuthorityCache");
			ShaderDesc desc{
				.m_SourcePath = L"Passes/PassForwardCoverage.hlsl",
				.m_Stage = ShaderStage::Vertex,
				.m_Entry = L"VSMain",
				.m_IncludeDirs = { L"." },
			};
			const ShaderResolvedRecipe recipe = compiler.Resolve(desc);

			ShaderResolvedRecipe foreignProducerRecipe = recipe;
			foreignProducerRecipe.m_CompilerIdentity.m_CanonicalIdentity += L"-different";
			const ShaderCompileResult foreignProducerResult =
				compiler.CompileOrLoad(foreignProducerRecipe);
			context.Check(!foreignProducerResult.IsSuccess() &&
				foreignProducerResult.m_Status == ShaderCompileStatus::InvalidRequest &&
				foreignProducerResult.m_Diagnostics.m_ValidationError ==
				ShaderCompileValidationError::CompilerIdentityMismatch,
				"CompileOrLoad rejects a recipe resolved by a different producer identity");

			ShaderResolvedRecipe mutatedRequestRecipe = recipe;
			mutatedRequestRecipe.m_Request.m_ExtraArgs.push_back(L"-DGGLAB_MUTATED=1");
			const ShaderCompileResult mutatedRequestResult =
				compiler.CompileOrLoad(mutatedRequestRecipe);
			context.Check(!mutatedRequestResult.IsSuccess() &&
				mutatedRequestResult.m_Status == ShaderCompileStatus::InvalidRequest,
				"CompileOrLoad rejects a recipe whose request was mutated after Resolve");

			ShaderResolvedRecipe mutatedIdentityRecipe = recipe;
			mutatedIdentityRecipe.m_RecipeId.m_DurableDigest.m_Value[0] ^= std::byte{ 0x01 };
			const ShaderCompileResult mutatedIdentityResult =
				compiler.CompileOrLoad(mutatedIdentityRecipe);
			context.Check(!mutatedIdentityResult.IsSuccess() &&
				mutatedIdentityResult.m_Status == ShaderCompileStatus::InvalidRequest,
				"CompileOrLoad rejects a recipe with a tampered identity instead of publishing under a stale one");

			ShaderResolvedRecipe mutatedSourceRecipe = recipe;
			mutatedSourceRecipe.m_LogicalSourcePath = L"Passes/PassForwardPBR.hlsl";
			const ShaderCompileResult mutatedSourceResult =
				compiler.CompileOrLoad(mutatedSourceRecipe);
			context.Check(!mutatedSourceResult.IsSuccess() &&
				mutatedSourceResult.m_Status == ShaderCompileStatus::InvalidRequest,
				"CompileOrLoad rejects a recipe whose logical source disagrees with its physical source");
		}

		void RunDependencyChangeTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode) /
				std::format("GGLabDependencyChange-{}", GetCurrentProcessId());
			context.Check(!errorCode, "Dependency-change test resolves a temporary root");
			if (errorCode)
			{
				return;
			}
			ScopedTestDirectory scopedDirectory(tempRoot);

			const std::filesystem::path sourceRoot = tempRoot / L"Sources";
			constexpr std::string_view SourceContent =
				"#include \"Probe.hlsli\"\n"
				"float4 VSMain(uint id : SV_VertexID) : SV_Position "
				"{ return float4(ProbeValue, 0, 0, 1); }\n";
			const bool filesWritten =
				WriteTextFile(sourceRoot / L"Probe.hlsli", "static const float ProbeValue = 0.25f;\n") &&
				WriteTextFile(sourceRoot / L"Probe.hlsl", SourceContent);

			ShaderCompiler compiler(sourceRoot, tempRoot / L"Cache");
			ShaderDesc desc{
				.m_SourcePath = L"Probe.hlsl",
				.m_Stage = ShaderStage::Vertex,
				.m_Entry = L"VSMain",
				.m_IncludeDirs = { L"." },
			};
			const ShaderResolvedRecipe recipe = compiler.Resolve(desc);
			const ShaderCompileResult firstResult = compiler.CompileOrLoad(recipe);
			const ShaderCompileResult cachedResult = compiler.CompileOrLoad(recipe);

			// Changing the include content must invalidate the entry; the
			// authoritative check is the content digest of the bytes DXC
			// consumed.
			const bool includeChanged = WriteTextFile(
				sourceRoot / L"Probe.hlsli", "static const float ProbeValue = 0.5f;\n");
			const ShaderCompileResult changedResult = compiler.CompileOrLoad(recipe);
			const ShaderCompileResult revalidatedResult = compiler.CompileOrLoad(recipe);

			context.Check(filesWritten && firstResult.IsSuccess() && cachedResult.m_FromCache &&
				includeChanged && changedResult.IsSuccess() && !changedResult.m_FromCache &&
				revalidatedResult.m_FromCache &&
				changedResult.m_Artifact.m_Manifest.m_BinaryContentDigest !=
				firstResult.m_Artifact.m_Manifest.m_BinaryContentDigest,
				"Changed include content rebuilds the entry and the new digest re-validates");

			// The digest is the sole validation authority. An include rewritten
			// with its previous mtime restored must still invalidate the entry:
			// acceptance is decided by the content digest, not by metadata
			// equality. The serialized contract no longer carries any mtime at
			// all, so there is no recorded-mtime fast path left to bypass this
			// check.
			const std::filesystem::path includePath = sourceRoot / L"Probe.hlsli";
			const std::filesystem::file_time_type includeWriteTime =
				std::filesystem::last_write_time(includePath, errorCode);
			const bool includeChangedSameMtime = !errorCode &&
				WriteTextFile(includePath, "static const float ProbeValue = 0.75f;\n");
			std::filesystem::last_write_time(includePath, includeWriteTime, errorCode);
			const bool includeWriteTimeRestored = !errorCode &&
				std::filesystem::last_write_time(includePath, errorCode) == includeWriteTime;
			const ShaderCompileResult sameMtimeChangedResult = compiler.CompileOrLoad(recipe);
			const ShaderCompileResult sameMtimeRevalidatedResult = compiler.CompileOrLoad(recipe);

			// A touched mtime with identical content must stay a hit: mtime
			// drift alone never invalidates an entry.
			const std::filesystem::file_time_type touchedWriteTime =
				includeWriteTime + std::chrono::seconds(1);
			std::filesystem::last_write_time(includePath, touchedWriteTime, errorCode);
			const bool includeTouched = !errorCode;
			const ShaderCompileResult touchedResult = compiler.CompileOrLoad(recipe);

			context.Check(includeChangedSameMtime && includeWriteTimeRestored &&
				sameMtimeChangedResult.IsSuccess() && !sameMtimeChangedResult.m_FromCache &&
				sameMtimeRevalidatedResult.m_FromCache,
				"Include content changed under a restored mtime is rejected by the content digest");
			context.Check(includeTouched && touchedResult.IsSuccess() && touchedResult.m_FromCache,
				"Identical content with a drifted mtime remains a cache hit");
		}

		void RunBinaryReadOnceTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode) /
				std::format("GGLabBinaryReadOnce-{}", GetCurrentProcessId());
			context.Check(!errorCode, "Binary read-once test resolves a temporary root");
			if (errorCode)
			{
				return;
			}
			ScopedTestDirectory scopedDirectory(tempRoot);

			// The read-once unit must be self-consistent: the digest is the
			// SHA-256 of the exact returned bytes for empty and non-empty
			// content alike.
			const std::filesystem::path probePath = tempRoot / L"Probe.bin";
			constexpr std::string_view ProbeContent = "binary read-once probe content";
			const bool probeWritten = WriteTextFile(probePath, ProbeContent);
			const std::optional<testing::BinaryReadWithDigest> probe =
				testing::ReadBinaryWithDigestOnce(probePath);
			const bool probeSelfConsistent = probe.has_value() &&
				probe->m_Binary.SizeInBytes() == ProbeContent.size() &&
				std::memcmp(probe->m_Binary.Data(), ProbeContent.data(),
					ProbeContent.size()) == 0 &&
				probe->m_Digest == ComputeSha256(std::span(
					reinterpret_cast<const std::byte*>(probe->m_Binary.Data()),
					probe->m_Binary.SizeInBytes()));
			const bool emptyProbeWritten = WriteTextFile(probePath, "");
			const std::optional<testing::BinaryReadWithDigest> emptyProbe =
				testing::ReadBinaryWithDigestOnce(probePath);
			const bool emptyProbeSelfConsistent = emptyProbe.has_value() &&
				emptyProbe->m_Binary.SizeInBytes() == 0 &&
				emptyProbe->m_Digest == ComputeSha256(std::span(
					reinterpret_cast<const std::byte*>(nullptr), std::size_t{ 0 }));
			context.Check(probeWritten && probeSelfConsistent &&
				emptyProbeWritten && emptyProbeSelfConsistent,
				"Binary read-once unit hashes exactly the bytes it returns");

			// Single-read invariant of LoadShaderArtifactCacheRecord, proven
			// through the test seam: exactly one binary read happens, and the
			// returned bytes hash to the manifest digest that validated them.
			const std::filesystem::path recordPath = tempRoot / L"Probe.json";
			const std::filesystem::path binaryPath = tempRoot / L"Probe.dxil";
			const std::string binaryBytes = "published binary probe bytes";
			ShaderArtifactCacheRecord record{};
			record.m_Binary = ShaderBinary(binaryBytes.size());
			std::memcpy(record.m_Binary.Data(), binaryBytes.data(), binaryBytes.size());
			record.m_Manifest.m_BinaryFormat = ShaderBinaryFormat::Dxil;
			record.m_Manifest.m_RecipeId.m_DurableDigest.m_Value[0] = std::byte{ 0x01 };
			record.m_Manifest.m_BuildKey.m_DurableDigest.m_Value[0] = std::byte{ 0x02 };
			record.m_Manifest.m_LogicalSourcePath = L"Probe.hlsl";
			ShaderArtifactDependency mainSource{};
			mainSource.m_LogicalPath = L"Probe.hlsl";
			mainSource.m_ContentDigest.m_Value[0] = std::byte{ 0x03 };
			record.m_Manifest.m_Dependencies.push_back(std::move(mainSource));
			record.m_PhysicalSourcePath = R"(C:\Repo\Probe.hlsl)";
			record.m_DependencyPhysicalPaths = { std::filesystem::path(R"(C:\Repo\Probe.hlsl)") };
			record.m_Manifest.m_BinaryContentDigest.m_Digest = ComputeSha256(std::span(
				reinterpret_cast<const std::byte*>(binaryBytes.data()), binaryBytes.size()));
			const bool recordWritten = WriteShaderArtifactCacheRecord(recordPath, record);
			const bool binaryWritten = WriteTextFile(binaryPath, binaryBytes);

			static int binaryReadOnceCallCount = 0;
			testing::OverrideBinaryReadOnce([](const std::filesystem::path& path) noexcept
				-> std::optional<testing::BinaryReadWithDigest>
				{
					++binaryReadOnceCallCount;
					return testing::ReadBinaryWithDigestOnce(path);
				});
			const std::optional<ShaderArtifactCacheRecord> loaded =
				LoadShaderArtifactCacheRecord(recordPath, binaryPath);

			context.Check(recordWritten && binaryWritten,
				"Read-once load fixtures are written");
			context.Check(loaded.has_value(),
				"Read-once load accepts the matching record and binary");
			context.Check(binaryReadOnceCallCount == 1,
				"Read-once load performs exactly one binary read");
			context.Check(loaded.has_value() &&
				loaded->m_Binary.SizeInBytes() == binaryBytes.size() &&
				std::memcmp(loaded->m_Binary.Data(), binaryBytes.data(),
					binaryBytes.size()) == 0,
				"Read-once load returns the exact read bytes");
			context.Check(loaded.has_value() &&
				ComputeSha256(std::span(
					reinterpret_cast<const std::byte*>(loaded->m_Binary.Data()),
					loaded->m_Binary.SizeInBytes())) ==
					record.m_Manifest.m_BinaryContentDigest.m_Digest,
				"Read-once load digest matches the manifest digest");

			// A digest mismatch must reject through the same single read
			// point; the validated digest describes the returned bytes.
			const bool binaryCorrupted = WriteTextFile(binaryPath, "different bytes");
			const std::optional<ShaderArtifactCacheRecord> mismatchLoaded =
				LoadShaderArtifactCacheRecord(recordPath, binaryPath);
			const bool mismatchRejected = !mismatchLoaded.has_value() &&
				binaryReadOnceCallCount == 2;
			testing::OverrideBinaryReadOnce(nullptr);

			context.Check(binaryCorrupted && mismatchRejected,
				"Load rejects digest mismatches through the same single read point");
		}

		void RunDirectoryRaceTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode) /
				std::format("GGLabDirectoryRace-{}", GetCurrentProcessId());
			context.Check(!errorCode, "Directory race test resolves a temporary root");
			if (errorCode)
			{
				return;
			}
			ScopedTestDirectory scopedDirectory(tempRoot);

			const std::filesystem::path raceDirectory =
				tempRoot / L"Deep" / L"Race" / L"Directory";
			bool firstCreated = false;
			bool secondCreated = false;
			std::thread firstCreator([&raceDirectory, &firstCreated]() noexcept
				{
					firstCreated = utils::CreateDirectoryIfNotExist(raceDirectory);
				});
			std::thread secondCreator([&raceDirectory, &secondCreated]() noexcept
				{
					secondCreated = utils::CreateDirectoryIfNotExist(raceDirectory);
				});
			firstCreator.join();
			secondCreator.join();
			const bool directoryExists = std::filesystem::exists(raceDirectory, errorCode);
			context.Check(firstCreated && secondCreated && directoryExists,
				"Concurrent directory creation treats the concurrently created directory as success");
		}

		void RunPublicationOutcomeTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode) /
				std::format("GGLabPublicationOutcome-{}", GetCurrentProcessId());
			context.Check(!errorCode, "Publication outcome test resolves a temporary root");
			if (errorCode)
			{
				return;
			}
			ScopedTestDirectory scopedDirectory(tempRoot);

			const std::filesystem::path sourceRoot = tempRoot / L"Sources";
			constexpr std::string_view SourceContent =
				"#include \"Probe.hlsli\"\n"
				"float4 VSMain(uint id : SV_VertexID) : SV_Position "
				"{ return float4(ProbeValue, 0, 0, 1); }\n";
			const bool filesWritten =
				WriteTextFile(sourceRoot / L"Probe.hlsl", SourceContent) &&
				WriteTextFile(sourceRoot / L"Probe.hlsli", "static const float ProbeValue = 0.25f;\n");
			const std::filesystem::path cacheRoot = tempRoot / L"Cache";
			ShaderDesc desc{
				.m_SourcePath = L"Probe.hlsl",
				.m_Stage = ShaderStage::Vertex,
				.m_Entry = L"VSMain",
				.m_IncludeDirs = { L"." },
			};

			// Baseline: producer A commits the slot with input X (0.25).
			ShaderCompiler compilerA(sourceRoot, cacheRoot);
			const ShaderCompileResult resultA = compilerA.Compile(desc);

			// Same-input convergence: with the slot already committed by A, the
			// cache-hit path returns the committed entry for the same input.
			ShaderCompiler compilerB(sourceRoot, cacheRoot);
			const ShaderCompileResult sameInputResult = compilerB.Compile(desc);
			const bool sameInputConverged = sameInputResult.IsSuccess() &&
				sameInputResult.m_FromCache &&
				sameInputResult.m_Artifact.m_Manifest == resultA.m_Artifact.m_Manifest;

			// Provenance conflict with a successful republish: the input
			// changes to Y (0.5), B compiles Y, its first publication loses
			// (renames fail), the conflict triggers republish-once, and the
			// second publication commits B's fresh product.
			static int publishFailuresRemaining = 0;
			const auto InjectCountedFailures = []() noexcept
				{
					testing::OverridePublishFileFailure([](
						const std::filesystem::path& /*destination*/) noexcept -> bool
						{
							if (publishFailuresRemaining > 0)
							{
								--publishFailuresRemaining;
								return true;
							}
							return false;
						});
				};
			const bool inputChangedForRepublish = WriteTextFile(
				sourceRoot / L"Probe.hlsli", "static const float ProbeValue = 0.5f;\n");
			// Only the binary rename is consumed: when it fails, the manifest
			// rename step is skipped entirely, so one injected failure loses
			// the first publication and the republish runs for real.
			publishFailuresRemaining = 1;
			InjectCountedFailures();
			const ShaderCompileResult republishedResult = compilerB.Compile(desc);
			testing::OverridePublishFileFailure(nullptr);
			const bool republishSucceeded = republishedResult.IsSuccess() &&
				!republishedResult.m_FromCache &&
				republishedResult.m_Artifact.m_Manifest.m_BinaryContentDigest !=
					resultA.m_Artifact.m_Manifest.m_BinaryContentDigest;

			// Unresolved provenance conflict: the input changes to Z (0.75),
			// B compiles Z, and every rename keeps failing, so both the first
			// publication and the republish observe the stale winner. The
			// conflict must surface as SourceChangedDuringCompile, never as
			// ArtifactIOFailure and never as a stale Success.
			const bool inputChangedForConflict = WriteTextFile(
				sourceRoot / L"Probe.hlsli", "static const float ProbeValue = 0.75f;\n");
			testing::OverridePublishFileFailure([](
				const std::filesystem::path& /*destination*/) noexcept -> bool
				{
					return true;
				});
			const ShaderCompileResult conflictResult = compilerB.Compile(desc);
			testing::OverridePublishFileFailure(nullptr);
			const bool conflictSurfaced = !conflictResult.IsSuccess() &&
				conflictResult.m_Status == ShaderCompileStatus::SourceChangedDuringCompile &&
				!conflictResult.m_Diagnostics.m_Message.empty();

			// Recovery: with the seam cleared, B publishes its fresh product
			// normally.
			const ShaderCompileResult recoveredResult = compilerB.Compile(desc);

			// Structural failure: a fresh cache root with no winner and all
			// renames failing can never observe a committed entry and must
			// map to ArtifactIOFailure.
			ShaderCompiler compilerC(sourceRoot, tempRoot / L"CacheC");
			testing::OverridePublishFileFailure([](
				const std::filesystem::path& /*destination*/) noexcept -> bool
				{
					return true;
				});
			const ShaderCompileResult structuralFailureResult = compilerC.Compile(desc);
			testing::OverridePublishFileFailure(nullptr);
			const bool structuralFailureSurfaced = !structuralFailureResult.IsSuccess() &&
				structuralFailureResult.m_Status == ShaderCompileStatus::ArtifactIOFailure;

			// CommittedByOther handoff: install a same-slot record with matching
			// dependency provenance and a valid binary but non-equivalent producer
			// metadata. Cache-hit validation rejects the changed compiler identity,
			// the failure seam preserves the record during publication, and
			// CompileOrLoad must return exactly that committed winner.
			ShaderCompiler compilerE(sourceRoot, tempRoot / L"CacheE");
			const ShaderResolvedRecipe winnerRecipe = compilerE.Resolve(desc);
			const ShaderCompileResult winnerBaseline = compilerE.CompileOrLoad(winnerRecipe);
			const std::filesystem::path winnerBinaryPath =
				compilerE.GetCacheBinaryPath(winnerRecipe);
			const std::filesystem::path winnerRecordPath =
				MakeCacheRecordPath(winnerBinaryPath);
			const std::optional<ShaderArtifactCacheRecord> winnerBaselineRecord =
				LoadShaderArtifactCacheRecord(winnerRecordPath, winnerBinaryPath);
			bool competingWinnerInstalled = false;
			ShaderCompileResult committedWinnerResult{};
			static int committedWinnerFailureCalls = 0;
			committedWinnerFailureCalls = 0;
			if (winnerRecipe.IsSuccess() && winnerBaseline.IsSuccess() &&
				winnerBaselineRecord.has_value() && winnerBaselineRecord->m_Binary.IsValid())
			{
				ShaderArtifactCacheRecord competingWinner = *winnerBaselineRecord;
				competingWinner.m_Manifest.m_CompilerIdentity.m_CanonicalIdentity +=
					L"+committed-winner";
				competingWinnerInstalled =
					OverwriteBinaryFile(winnerBinaryPath, competingWinner.m_Binary) &&
					WriteShaderArtifactCacheRecord(winnerRecordPath, competingWinner);
				testing::OverridePublishFileFailure([](
					const std::filesystem::path& /*destination*/) noexcept -> bool
					{
						++committedWinnerFailureCalls;
						return true;
					});
				committedWinnerResult = compilerE.CompileOrLoad(winnerRecipe);
				testing::OverridePublishFileFailure(nullptr);
			}
			const std::optional<ShaderArtifactCacheRecord> observedWinner =
				LoadShaderArtifactCacheRecord(winnerRecordPath, winnerBinaryPath);
			const bool committedWinnerReturned = competingWinnerInstalled &&
				committedWinnerFailureCalls > 0 && committedWinnerResult.IsSuccess() &&
				committedWinnerResult.m_FromCache && observedWinner.has_value() &&
				committedWinnerResult.m_Artifact.m_Manifest == observedWinner->m_Manifest &&
				committedWinnerResult.m_Artifact.m_Binary.SizeInBytes() ==
					observedWinner->m_Binary.SizeInBytes() &&
				std::memcmp(committedWinnerResult.m_Artifact.m_Binary.Data(),
					observedWinner->m_Binary.Data(), observedWinner->m_Binary.SizeInBytes()) == 0;

			// Slot structural binding: a committed record can be parseable and
			// binary-digest-valid while carrying a RecipeId that does not belong
			// to the current operation. BuildKey equality alone must never let
			// that record classify as CommittedByOther or reach Success through
			// matching dependency provenance.
			ShaderCompiler compilerD(sourceRoot, tempRoot / L"CacheD");
			const ShaderResolvedRecipe bindingRecipe = compilerD.Resolve(desc);
			const ShaderCompileResult bindingBaseline = compilerD.CompileOrLoad(bindingRecipe);
			const std::filesystem::path bindingBinaryPath =
				compilerD.GetCacheBinaryPath(bindingRecipe);
			const std::filesystem::path bindingRecordPath =
				MakeCacheRecordPath(bindingBinaryPath);
			const std::optional<ShaderArtifactCacheRecord> bindingBaselineRecord =
				LoadShaderArtifactCacheRecord(bindingRecordPath, bindingBinaryPath);
			bool mismatchedRecipeRecordInstalled = false;
			ShaderPublicationResult mismatchedRecipePublication{};
			ShaderCompileResult mismatchedRecipeCompile{};
			if (bindingRecipe.IsSuccess() && bindingBaseline.IsSuccess() &&
				bindingBaselineRecord.has_value())
			{
				ShaderArtifactCacheRecord mismatchedRecipeRecord = *bindingBaselineRecord;
				mismatchedRecipeRecord.m_Manifest.m_RecipeId.m_DurableDigest.m_Value[0] ^=
					std::byte{ 0x01 };
				mismatchedRecipeRecordInstalled =
					WriteShaderArtifactCacheRecord(bindingRecordPath, mismatchedRecipeRecord);
				testing::OverridePublishFileFailure([](
					const std::filesystem::path& /*destination*/) noexcept -> bool
					{
						return true;
					});
				mismatchedRecipePublication = PublishShaderArtifactCacheRecord(
					bindingBinaryPath, bindingRecordPath, *bindingBaselineRecord);
				mismatchedRecipeCompile = compilerD.CompileOrLoad(bindingRecipe);
				testing::OverridePublishFileFailure(nullptr);
			}
			const bool mismatchedRecipeRejected = mismatchedRecipeRecordInstalled &&
				mismatchedRecipePublication.m_Outcome == ShaderPublicationOutcome::Failed &&
				!mismatchedRecipeCompile.IsSuccess() &&
				mismatchedRecipeCompile.m_Status == ShaderCompileStatus::ArtifactIOFailure;

			context.Check(filesWritten && resultA.IsSuccess() && sameInputConverged,
				"Same-input concurrent producers converge on the committed entry");
			context.Check(inputChangedForRepublish && republishSucceeded,
				"Provenance conflict republishes once and commits the fresh product");
			context.Check(inputChangedForConflict && conflictSurfaced,
				"Unresolved provenance conflict maps to SourceChangedDuringCompile, never stale Success");
			context.Check(recoveredResult.IsSuccess(),
				"Clearing the failure seam recovers normal publication");
			context.Check(structuralFailureSurfaced,
				"Publication without any committed observation maps to ArtifactIOFailure");
			context.Check(committedWinnerReturned,
				"CommittedByOther returns the exact committed winner artifact used by CLI identity fields");
			context.Check(mismatchedRecipeRejected,
				"Publication rejects a committed same-BuildKey entry with a mismatched RecipeId");
		}

		void RunCacheRecordSerializationTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode) /
				std::format("GGLabCacheRecordSerialization-{}", GetCurrentProcessId());
			context.Check(!errorCode, "Cache record serialization test resolves a temporary root");
			if (errorCode)
			{
				return;
			}
			ScopedTestDirectory scopedDirectory(tempRoot);
			const std::filesystem::path recordPath = tempRoot / L"Probe.json";

			// Schema=2 round-trip through the public API: portable dependency
			// provenance lives in the manifest, physical resolution lives in
			// the local record, index-corresponding.
			ShaderArtifactCacheRecord record{};
			ShaderArtifactManifest& manifest = record.m_Manifest;
			manifest.m_RecipeId.m_DurableDigest.m_Value[0] = std::byte{ 0xAB };
			manifest.m_BuildKey.m_DurableDigest.m_Value[31] = std::byte{ 0xCD };
			manifest.m_BinaryContentDigest.m_Digest.m_Value[16] = std::byte{ 0xEF };
			manifest.m_TargetProfile = ShaderTargetProfile::GGLabVulkan13;
			manifest.m_BinaryFormat = ShaderBinaryFormat::SpirV;
			manifest.m_Stage = ShaderStage::Compute;
			manifest.m_LogicalSourcePath = L"Passes/Probe.hlsl";
			manifest.m_EntryPoint = L"CSMain";
			manifest.m_Defines = { L"GGLAB_TEST=1", L"PLAIN" };
			manifest.m_LogicalIncludeDirs = { L".", L"Common" };
			manifest.m_ExtraArgs = { L"-DGGLAB_SERIALIZATION=1" };
			for (std::size_t index = 0; index < 3; ++index)
			{
				ShaderArtifactDependency dependency{};
				dependency.m_LogicalPath = (index == 0)
					? std::filesystem::path(L"Passes/Probe.hlsl")
					: std::filesystem::path(std::format(L"Passes/Dep{}.hlsl", index));
				dependency.m_ContentDigest.m_Value[0] =
					std::byte{ static_cast<unsigned char>(index + 1) };
				manifest.m_Dependencies.push_back(std::move(dependency));
			}
			record.m_PhysicalSourcePath = L"C:\\Repo\\Shaders\\Passes\\Probe.hlsl";
			record.m_PhysicalIncludeDirs = { L"C:\\Repo\\Shaders" };
			record.m_DependencyPhysicalPaths = {
				std::filesystem::path(L"C:\\Repo\\Shaders\\Passes\\Probe.hlsl"),
				std::filesystem::path(R"(C:\Repo\PhysB.hlsl)"),
				std::filesystem::path(R"(C:\Repo\PhysC.hlsl)"),
			};

			const bool written = WriteShaderArtifactCacheRecord(recordPath, record);
			const std::optional<ShaderArtifactCacheRecord> readBack =
				ReadShaderArtifactCacheRecord(recordPath);
			bool roundTripped = readBack.has_value() &&
				readBack->m_Manifest.m_RecipeId == record.m_Manifest.m_RecipeId &&
				readBack->m_Manifest.m_BuildKey == record.m_Manifest.m_BuildKey &&
				readBack->m_Manifest.m_BinaryContentDigest ==
					record.m_Manifest.m_BinaryContentDigest &&
				readBack->m_Manifest.m_TargetProfile == record.m_Manifest.m_TargetProfile &&
				readBack->m_Manifest.m_BinaryFormat == record.m_Manifest.m_BinaryFormat &&
				readBack->m_Manifest.m_Stage == record.m_Manifest.m_Stage &&
				readBack->m_Manifest.m_LogicalSourcePath == record.m_Manifest.m_LogicalSourcePath &&
				readBack->m_Manifest.m_EntryPoint == record.m_Manifest.m_EntryPoint &&
				readBack->m_Manifest.m_Defines == record.m_Manifest.m_Defines &&
				readBack->m_Manifest.m_LogicalIncludeDirs ==
					record.m_Manifest.m_LogicalIncludeDirs &&
				readBack->m_Manifest.m_ExtraArgs == record.m_Manifest.m_ExtraArgs &&
				readBack->m_PhysicalSourcePath == record.m_PhysicalSourcePath &&
				readBack->m_PhysicalIncludeDirs == record.m_PhysicalIncludeDirs &&
				readBack->m_Manifest.m_Dependencies.size() ==
					record.m_Manifest.m_Dependencies.size() &&
				readBack->m_DependencyPhysicalPaths.size() ==
					record.m_DependencyPhysicalPaths.size();
			for (std::size_t index = 0; roundTripped &&
				index < record.m_Manifest.m_Dependencies.size(); ++index)
			{
				roundTripped = roundTripped &&
					readBack->m_Manifest.m_Dependencies[index].m_LogicalPath ==
						record.m_Manifest.m_Dependencies[index].m_LogicalPath &&
					readBack->m_Manifest.m_Dependencies[index].m_ContentDigest ==
						record.m_Manifest.m_Dependencies[index].m_ContentDigest &&
					readBack->m_DependencyPhysicalPaths[index] ==
						record.m_DependencyPhysicalPaths[index];
			}
			context.Check(written && roundTripped,
				"Cache record schema=2 round-trips provenance and physical resolution index-wise");

			const std::optional<std::string> serializedManifest =
				SerializeShaderArtifactManifest(record.m_Manifest);
			const std::optional<ShaderArtifactManifest> manifestRoundTrip =
				serializedManifest.has_value()
				? DeserializeShaderArtifactManifest(*serializedManifest)
				: std::nullopt;
			const bool manifestOnlyRoundTripped = manifestRoundTrip.has_value() &&
				*manifestRoundTrip == record.m_Manifest;
			context.Check(manifestOnlyRoundTripped,
				"Manifest-only API round-trips the complete portable manifest and dependency provenance");

			const bool manifestOnlyPortable = serializedManifest.has_value() &&
				serializedManifest->find("\"dependencies\":[") != std::string::npos &&
				serializedManifest->find("\"logicalPath\":") != std::string::npos &&
				serializedManifest->find("\"contentDigest\":") != std::string::npos &&
				serializedManifest->find("physicalSource") == std::string::npos &&
				serializedManifest->find("physicalIncludeDirs") == std::string::npos &&
				serializedManifest->find("dependencyPhysicalPaths") == std::string::npos &&
				serializedManifest->find("lastWriteTimeTicks") == std::string::npos &&
				serializedManifest->find("C:\\\\Repo") == std::string::npos;
			context.Check(manifestOnlyPortable,
				"Manifest-only serialization carries provenance without physical paths or mtime state");

			// Domain strictness: the document baseline is the emitted schema=2
			// record; each tamper must reject the document as a cache miss.
			const auto ReadRecordText = [&recordPath]() noexcept -> std::string
				{
					std::ifstream input(recordPath, std::ios::binary);
					return input
						? std::string((std::istreambuf_iterator<char>(input)),
							std::istreambuf_iterator<char>())
						: std::string{};
				};
			const std::string baseline = ReadRecordText();
			const auto RestoreBaseline = [&recordPath, &baseline]() noexcept -> bool
				{
					return WriteTextFile(recordPath, baseline);
				};

			const bool duplicateKeyTampered = ReplaceMetadataValue(recordPath,
				"\"schemaVersion\":2", "\"schemaVersion\":2,\"schemaVersion\":2");
			const bool duplicateKeyRejected =
				!ReadShaderArtifactCacheRecord(recordPath).has_value();
			RestoreBaseline();

			const bool unknownFieldTampered = ReplaceMetadataValue(recordPath,
				"\"schemaVersion\":2", "\"schemaVersion\":2,\"unknownField\":7");
			const bool unknownFieldRejected =
				!ReadShaderArtifactCacheRecord(recordPath).has_value();
			RestoreBaseline();

			const bool typeMismatchTampered = ReplaceMetadataValue(recordPath,
				"\"schemaVersion\":2", "\"schemaVersion\":\"2\"");
			const bool typeMismatchRejected =
				!ReadShaderArtifactCacheRecord(recordPath).has_value();
			RestoreBaseline();

			const bool unsupportedSchemaTampered = ReplaceMetadataValue(recordPath,
				"\"schemaVersion\":2", "\"schemaVersion\":999");
			const bool unsupportedSchemaRejected =
				!ReadShaderArtifactCacheRecord(recordPath).has_value();
			RestoreBaseline();

			// Dependency cardinality invariant: dropping one physical
			// resolution must reject the whole record.
			const bool cardinalityTampered = ReplaceMetadataValue(recordPath,
				R"("dependencyPhysicalPaths":["C:\\Repo\\Shaders\\Passes\\Probe.hlsl","C:\\Repo\\PhysB.hlsl","C:\\Repo\\PhysC.hlsl"])",
				R"("dependencyPhysicalPaths":["C:\\Repo\\Shaders\\Passes\\Probe.hlsl","C:\\Repo\\PhysB.hlsl"])");
			const bool cardinalityRejected =
				!ReadShaderArtifactCacheRecord(recordPath).has_value();
			RestoreBaseline();

			// The record schema and the manifest schema are independent axes:
			// bumping either one alone rejects the document.
			const bool recordSchemaBumped = ReplaceMetadataValue(recordPath,
				"\"recordSchemaVersion\":2", "\"recordSchemaVersion\":3");
			const bool recordSchemaBumpedRejected =
				!ReadShaderArtifactCacheRecord(recordPath).has_value();
			RestoreBaseline();

			const bool manifestSchemaBumped = ReplaceMetadataValue(recordPath,
				"\"schemaVersion\":2", "\"schemaVersion\":3");
			const bool manifestSchemaBumpedRejected =
				!ReadShaderArtifactCacheRecord(recordPath).has_value();
			RestoreBaseline();

			// Main-source binding structural invariants: the first dependency
			// must describe the main source, in both its portable and local
			// forms. An empty dependency list, a mismatched first logical
			// path, or a mismatched first physical resolution must reject the
			// record as invalid derived data.
			const auto WriteAndReject = [&recordPath](
				const ShaderArtifactCacheRecord& probe) noexcept
				{
					return WriteShaderArtifactCacheRecord(recordPath, probe) &&
						!ReadShaderArtifactCacheRecord(recordPath).has_value();
				};

			ShaderArtifactCacheRecord emptyDependencies = record;
			emptyDependencies.m_Manifest.m_Dependencies.clear();
			emptyDependencies.m_DependencyPhysicalPaths.clear();
			const bool emptyDependenciesRejected = WriteAndReject(emptyDependencies);

			ShaderArtifactCacheRecord mismatchedLogicalSource = record;
			mismatchedLogicalSource.m_Manifest.m_Dependencies[0].m_LogicalPath =
				std::filesystem::path(L"Other/Main.hlsl");
			const bool mismatchedLogicalSourceRejected = WriteAndReject(mismatchedLogicalSource);

			ShaderArtifactCacheRecord mismatchedPhysicalSource = record;
			mismatchedPhysicalSource.m_DependencyPhysicalPaths[0] =
				std::filesystem::path(R"(C:\Repo\Other.hlsl)");
			const bool mismatchedPhysicalSourceRejected = WriteAndReject(mismatchedPhysicalSource);

			RestoreBaseline();

			// Portability self-check: the manifest sub-document carries the
			// dependency provenance but none of the physical resolutions.
			const std::size_t manifestBegin = baseline.find("\"manifest\":{");
			const std::size_t manifestEnd = baseline.find(",\"recordSchemaVersion\":");
			const bool manifestSectionFound = manifestBegin != std::string::npos &&
				manifestEnd != std::string::npos && manifestEnd > manifestBegin;
			const std::string manifestSection = manifestSectionFound
				? baseline.substr(manifestBegin, manifestEnd - manifestBegin)
				: std::string{};
			const bool manifestSectionPortable =
				manifestSection.find("\"logicalPath\"") != std::string::npos &&
				manifestSection.find("\"contentDigest\"") != std::string::npos &&
				manifestSection.find("Phys") == std::string::npos &&
				manifestSection.find("Repo") == std::string::npos &&
				manifestSection.find("dependencyPhysicalPaths") == std::string::npos;

			std::string deepDocument;
			for (int depth = 0; depth < 80; ++depth)
			{
				deepDocument += "{\"x\":";
			}
			deepDocument += "0";
			for (int depth = 0; depth < 80; ++depth)
			{
				deepDocument += '}';
			}
			const bool deepDocumentWritten = WriteTextFile(recordPath, deepDocument);
			const bool deepDocumentRejected =
				!ReadShaderArtifactCacheRecord(recordPath).has_value();
			RestoreBaseline();

			// Integer domain: unsigned integers beyond INT64_MAX must be
			// rejected instead of being arithmetically converted into the
			// int64 fields.
			const bool overflowTampered = OverwriteJsonIntegerField(
				recordPath, "bindingAbiRevision", "9223372036854775808");
			const bool overflowRejected =
				!ReadShaderArtifactCacheRecord(recordPath).has_value();
			RestoreBaseline();

			const bool uint64MaxTampered = OverwriteJsonIntegerField(
				recordPath, "bindingAbiRevision", "18446744073709551615");
			const bool uint64MaxRejected =
				!ReadShaderArtifactCacheRecord(recordPath).has_value();
			RestoreBaseline();

			// Cache epoch evidence: a legacy schema=1 document (previous
			// serializer shape, including the removed mtime field) is rejected
			// as an unknown schema.
			const std::filesystem::path legacyPath = tempRoot / L"Legacy.json";
			const std::string legacyFixture =
				R"({"manifest":{"schemaVersion":1,"recipeHashSchema":1,)"
				R"("recipeId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",)"
				R"("buildKey":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",)"
				R"("compiler":{"kind":"dxc","identity":"6.7.2105.12+1.abcd"},)"
				R"("targetProfile":"gglab-dx12","binaryFormat":"dxil","spirvTargetEnvironment":"none",)"
				R"("bindingAbiRevision":0,"coordinateOptions":0,"stage":"vertex","shaderModel":"6_7",)"
				R"("hlslVersion":"2021","compileFlags":1,"optimizationLevel":"O3",)"
				R"("logicalSource":"Passes/LegacyProbe.hlsl","entryPoint":"VSMain","target":"vs_6_7",)"
				R"("defines":[],"logicalIncludeDirs":[".","Common"],"extraArgs":["-DGGLAB_LEGACY=1"],)"
				R"("binaryContentDigest":"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"},)"
				R"("local":{"physicalSource":"C:\\Legacy\\Shaders\\Passes\\LegacyProbe.hlsl",)"
				R"("physicalIncludeDirs":["C:\\Legacy\\Shaders"],)"
				R"("dependencies":[{"logicalPath":"Passes/LegacyProbe.hlsl",)"
				R"("physicalPath":"C:\\Legacy\\Shaders\\Passes\\LegacyProbe.hlsl",)"
				R"("contentDigest":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",)"
				R"("lastWriteTimeTicks":9007199254740993}]}})";
			const bool legacyFixtureWritten = WriteTextFile(legacyPath, legacyFixture);
			const bool legacyRejected =
				!ReadShaderArtifactCacheRecord(legacyPath).has_value();

			context.Check(!baseline.empty() && duplicateKeyTampered && duplicateKeyRejected,
				"Cache record reader rejects duplicate object keys");
			context.Check(unknownFieldTampered && unknownFieldRejected,
				"Cache record reader rejects unknown fields");
			context.Check(typeMismatchTampered && typeMismatchRejected,
				"Cache record reader rejects type mismatches");
			context.Check(unsupportedSchemaTampered && unsupportedSchemaRejected,
				"Cache record reader rejects unsupported schema versions");
			context.Check(cardinalityTampered && cardinalityRejected,
				"Cache record reader rejects dependency cardinality mismatches");
			context.Check(recordSchemaBumped && recordSchemaBumpedRejected &&
				manifestSchemaBumped && manifestSchemaBumpedRejected,
				"Record and manifest schema versions are independent rejection axes");
			context.Check(emptyDependenciesRejected && mismatchedLogicalSourceRejected &&
				mismatchedPhysicalSourceRejected,
				"Cache record reader enforces the main-source binding invariants");
			context.Check(manifestSectionFound && manifestSectionPortable,
				"Manifest sub-document carries provenance without any physical path");
			context.Check(deepDocumentWritten && deepDocumentRejected,
				"Cache record reader rejects documents beyond the nesting depth limit");
			context.Check(overflowTampered && overflowRejected &&
				uint64MaxTampered && uint64MaxRejected,
				"Cache record reader rejects unsigned integers beyond INT64_MAX");
			context.Check(legacyFixtureWritten && legacyRejected,
				"Legacy schema=1 documents are rejected as the intentional cache epoch");
		}

	}

	void RunShaderCompileContractSelfTests(SelfTestContext& context) noexcept
	{
		RunShaderBindingABITests(context);
		RunCoordinatePolicyTests(context);
		RunBinaryReadOnceTests(context);
		RunCacheRecordSerializationTests(context);
		RunDirectoryRaceTests(context);
		RunPublicationOutcomeTests(context);
		RunShaderArtifactContractTests(context);
		RunCrossCheckoutIdentityTests(context);
		RunRecipeAuthorityTests(context);
		RunDependencyChangeTests(context);
	}
}
