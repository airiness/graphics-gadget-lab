#include "ShaderCompileContractSelfTests.h"

#include "SpirVDecorationReader.h"
#include "Compiler/ShaderCompiler.h"
#include "GGLabFoundation/Json/JsonValue.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "Graphics/RHI/RHICoordinatePolicy.h"
#include "Graphics/RHI/Vulkan/VulkanCoordinatePolicy.h"
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/Shader/ShaderPaths.h"
#include "Targets/Vulkan13ShaderTarget.h"
#include "Targets/VulkanShaderCompileABI.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
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

		[[nodiscard]] std::filesystem::path MakeManifestPath(
			const std::filesystem::path& binaryPath) noexcept
		{
			auto manifestPath = binaryPath;
			manifestPath += L".json";
			return manifestPath;
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
			context.Check(GGLabVulkanShaderCompileABI.m_Revision == 1,
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
						registerIndex, GGLabVulkanShaderCompileABI.m_FixedHlslRegisterSpace);
					const uint32_t expectedBinding = range.m_BindingShift + registerIndex;
					allBindingsUnique &= result.IsSupported() &&
						result.m_Location.m_DescriptorSet ==
						GGLabVulkanShaderCompileABI.m_FixedDescriptorSet &&
						result.m_Location.m_Binding == expectedBinding &&
						expectedBinding < occupiedBindings.size() && !occupiedBindings[expectedBinding];
					if (expectedBinding < occupiedBindings.size())
					{
						occupiedBindings[expectedBinding] = true;
					}
				}
				const auto outOfRange = EvaluateVulkanFixedShaderBinding(testCase.m_RegisterClass,
					range.m_RegisterCount, GGLabVulkanShaderCompileABI.m_FixedHlslRegisterSpace);
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
				GGLabVulkanShaderCompileABI.m_GlobalHeapHlslRegisterSpace);
			context.Check(!reservedSpace.IsSupported() && reservedSpace.m_RejectionReason ==
				VulkanShaderBindingRejectionReason::ReservedGlobalHeapRegisterSpace,
				"Fixed bindings reject HLSL space1 reserved for global heaps");
			const auto unsupportedSpace = EvaluateVulkanFixedShaderBinding(
				VulkanShaderRegisterClass::ShaderResource, 0,
				GGLabVulkanShaderCompileABI.m_GlobalHeapHlslRegisterSpace + 1);
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
			context.Check(GGLabVulkanShaderCompileABI.m_InvertVertexProducingStageY &&
				GGLabVulkanShaderCompileABI.m_UseDxPositionW &&
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

			ShaderDesc managerDesc{
				.m_SourcePath = L"Passes/PassForwardCoverage.hlsl",
				.m_Stage = ShaderStage::Vertex,
				.m_Target = MakeVulkan13CompileTarget(ShaderStage::Vertex),
				.m_Entry = L"VSMain",
			};
			ShaderManager dxilManager(
				RHIBackendType::DX12, shaderSourceRoot, shaderCacheRoot);
			const ShaderID dxilManagerShader = dxilManager.LoadShader(managerDesc);
			managerDesc.m_Target = {};
			ShaderManager spirVManager(
				RHIBackendType::Vulkan, shaderSourceRoot, shaderCacheRoot);
			const ShaderID spirVManagerShader = spirVManager.LoadShader(managerDesc);
			context.Check(dxilManager.GetActiveBackend() == RHIBackendType::DX12 &&
				spirVManager.GetActiveBackend() == RHIBackendType::Vulkan &&
				dxilManagerShader.IsValid() && spirVManagerShader.IsValid() &&
				dxilManager.GetBytecode(dxilManagerShader).m_Format == ShaderBinaryFormat::Dxil &&
				spirVManager.GetBytecode(spirVManagerShader).m_Format == ShaderBinaryFormat::SpirV,
				"ShaderManager derives shader format from its active RHI backend");

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
					std::to_wstring(GGLabVulkanShaderCompileABI.m_FixedHlslRegisterSpace);
				registerShiftsMatch &=
					ContainsArgumentSequence(vertexArguments, { option, shift, hlslSpace });
			}
			const std::wstring resourceBinding =
				std::to_wstring(GGLabVulkanShaderCompileABI.m_ResourceHeapBinding);
			const std::wstring samplerBinding =
				std::to_wstring(GGLabVulkanShaderCompileABI.m_SamplerHeapBinding);
			const std::wstring descriptorSet =
				std::to_wstring(GGLabVulkanShaderCompileABI.m_GlobalDescriptorSet);
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

			const std::filesystem::path spirVMetaPath = MakeManifestPath(spirVBinaryPath);
			std::ifstream metadataInput(spirVMetaPath, std::ios::binary);
			const std::string metadata((std::istreambuf_iterator<char>(metadataInput)),
				std::istreambuf_iterator<char>());
			context.Check(metadata.find("\"schemaVersion\":1") != std::string::npos &&
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
				metadata.find("\"lastWriteTimeTicks\":") != std::string::npos,
				"Cache record carries the complete portable manifest (profile, target semantics, logical identities) plus explicit local validation state");
			const bool metadataChanged = ReplaceMetadataValue(
				spirVMetaPath, "\"bindingAbiRevision\":1", "\"bindingAbiRevision\":999");
			const ShaderCompileResult rejectedCacheArtifact = compiler.CompileOrLoad(spirVRecipe);
			context.Check(metadataChanged && !rejectedCacheArtifact.m_FromCache,
				"Shader cache rejects metadata whose target contract does not match the recipe");

			// A missing required durable field must also reject the entry.
			const bool manifestFieldRemoved = ReplaceMetadataValue(
				spirVMetaPath, "\"hlslVersion\":\"2021\",", "");
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

			// Changing the include content must invalidate the entry even when
			// the mtime fast path would see a difference; the authoritative
			// check is the content digest of the bytes DXC consumed.
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
		}

		void RunJsonGrammarTests(SelfTestContext& context) noexcept
		{
			using gglab::json::JsonValue;

			const auto parseRoundTrip = [](std::string_view text, bool& parsed) noexcept
				{
					const std::optional<JsonValue> value = gglab::json::ParseJsonDocument(text);
					parsed = value.has_value();
				};

			// External-producer compatibility: whitespace, escaped solidus,
			// astral-plane surrogate pairs, and exponent numbers.
			bool whitespaceParsed = false;
			parseRoundTrip("  {\n\t\"a\" : [ true , null , 1.5e3 ]\r\n}  ", whitespaceParsed);
			const std::optional<JsonValue> surrogateValue = gglab::json::ParseJsonDocument(
				"{\"emoji\":\"\\uD83D\\uDE00\"}");
			const std::optional<JsonValue> escapedSolidus = gglab::json::ParseJsonDocument(
				"{\"url\":\"https:\\/\\/gglab\"}");
			context.Check(whitespaceParsed && surrogateValue.has_value() &&
				surrogateValue->IsObject() &&
				surrogateValue->AsObject().front().second.AsString() ==
				std::string("\xF0\x9F\x98\x80") &&
				escapedSolidus.has_value(),
				"JSON parser accepts whitespace, exponent numbers, escaped solidus, and astral surrogate pairs");

			// Strictness: structural errors must be rejected, never guessed.
			bool trailingGarbageParsed = true;
			parseRoundTrip("{}x", trailingGarbageParsed);
			bool duplicateKeyParsed = true;
			parseRoundTrip("{\"a\":1,\"a\":2}", duplicateKeyParsed);
			bool leadingZeroParsed = true;
			parseRoundTrip("{\"a\":01}", leadingZeroParsed);
			bool loneLowSurrogateParsed = true;
			parseRoundTrip("{\"a\":\"\\uDE00\"}", loneLowSurrogateParsed);
			bool badEscapeParsed = true;
			parseRoundTrip("{\"a\":\"\\q\"}", badEscapeParsed);
			bool rawControlParsed = true;
			parseRoundTrip(std::string("{\"a\":\"\x01\"}"), rawControlParsed);
			context.Check(!trailingGarbageParsed && !duplicateKeyParsed && !leadingZeroParsed &&
				!loneLowSurrogateParsed && !badEscapeParsed && !rawControlParsed,
				"JSON parser rejects trailing content, duplicate keys, leading zeros, lone surrogates, bad escapes, and raw control characters");

			// The emitter must produce grammar the parser accepts: nested
			// containers and member separators round-trip exactly.
			gglab::json::JsonWriter writer;
			writer.BeginObject();
			writer.Key("schemaVersion");
			writer.WriteInteger(1);
			writer.Key("nested");
			writer.BeginObject();
			writer.Key("flags");
			writer.WriteInteger(-2);
			writer.Key("list");
			writer.BeginArray();
			writer.WriteString("a");
			writer.WriteBool(true);
			writer.WriteNull();
			writer.EndArray();
			writer.EndObject();
			writer.Key("empty");
			writer.BeginArray();
			writer.EndArray();
			writer.EndObject();
			const std::string emitted = std::move(writer).Finish();
			const std::optional<JsonValue> reparsed = gglab::json::ParseJsonDocument(emitted);
			bool writerStructureValid = false;
			if (reparsed.has_value() && reparsed->IsObject())
			{
				const gglab::json::JsonObject& root = reparsed->AsObject();
				writerStructureValid = root.size() == 3 &&
					root.front().first == "schemaVersion" &&
					root.front().second.AsInteger() == 1 &&
					root.back().second.IsArray() &&
					root.back().second.AsArray().empty();
			}
			context.Check(writerStructureValid && emitted.find(",,") == std::string::npos,
				"JSON emitter produces single-separator documents that re-parse with preserved structure");
		}

	}

	void RunShaderCompileContractSelfTests(SelfTestContext& context) noexcept
	{
		RunShaderBindingABITests(context);
		RunCoordinatePolicyTests(context);
		RunJsonGrammarTests(context);
		RunShaderArtifactContractTests(context);
		RunCrossCheckoutIdentityTests(context);
		RunRecipeAuthorityTests(context);
		RunDependencyChangeTests(context);
	}
}
