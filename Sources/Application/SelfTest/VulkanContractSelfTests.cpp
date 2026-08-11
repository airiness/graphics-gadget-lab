#include "Core/Precompiled.h"
#include "Application/SelfTest/SpirVDecorationReader.h"
#include "Application/SelfTest/VulkanContractSelfTests.h"
#include "Application/ApplicationLaunchOptions.h"
#include "Graphics/RHI/RHICoordinatePolicy.h"
#include "Graphics/RHI/RHIDescriptorCapacityContract.h"
#include "Graphics/RHI/RHISampler.h"
#include "Graphics/RHI/RHITextureValidation.h"
#include "Graphics/RHI/Vulkan/VulkanBarrier.h"
#include "Graphics/RHI/Vulkan/VulkanCommandContext.h"
#include "Graphics/RHI/Vulkan/VulkanCoordinatePolicy.h"
#include "Graphics/RHI/Vulkan/VulkanDeviceProfile.h"
#include "Graphics/RHI/Vulkan/VulkanDescriptorManager.h"
#include "Graphics/RHI/Vulkan/VulkanDynamicUniformBuffer.h"
#include "Graphics/RHI/Vulkan/VulkanFormat.h"
#include "Graphics/RHI/Vulkan/VulkanConversions.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineState.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"
#include "Graphics/RHI/Vulkan/VulkanResourceManager.h"
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"
#include "Graphics/RHI/Vulkan/VulkanTextureCopy.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderManager.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanBootstrap.h"
#endif

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
			std::string_view key, std::string_view replacement) noexcept
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return false;
			}
			std::string content((std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			const std::string prefix = std::string(key) + "=";
			const size_t begin = content.find(prefix);
			const size_t end = begin == std::string::npos ? std::string::npos : content.find('\n', begin);
			if (begin == std::string::npos || end == std::string::npos)
			{
				return false;
			}
			content.replace(begin, end - begin, prefix + std::string(replacement));
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(content.data(), static_cast<std::streamsize>(content.size()));
			return output.good();
		}

		VulkanDeviceProfileCapabilities MakeSupportedCapabilities() noexcept
		{
			const uint32_t resourceCount =
				GGLabDescriptorCapacityContract.m_ResourceDescriptorCount;
			const uint32_t samplerCount =
				GGLabDescriptorCapacityContract.m_SamplerDescriptorCount;
			const uint32_t combinedCount = resourceCount + samplerCount;
			return {
				.m_IsWindowsX64 = true,
				.m_HasVulkanLoader = true,
				.m_ApiVersion = { 1, 3 },
				.m_HasWin32SurfaceExtension = true,
				.m_HasSwapchainExtension = true,
				.m_HasGraphicsPresentQueue = true,
				.m_DynamicRendering = true,
				.m_Synchronization2 = true,
				.m_TimelineSemaphore = true,
				.m_ScalarBlockLayout = true,
				.m_SamplerAnisotropy = true,
				.m_ShaderStorageImageExtendedFormats = true,
				.m_RuntimeDescriptorArray = true,
				.m_DescriptorBindingPartiallyBound = true,
				.m_DescriptorBindingUpdateUnusedWhilePending = true,
				.m_DescriptorBindingSampledImageUpdateAfterBind = true,
				.m_DescriptorBindingStorageImageUpdateAfterBind = true,
				.m_ShaderSampledImageArrayNonUniformIndexing = true,
				.m_ShaderStorageImageArrayNonUniformIndexing = true,
				.m_HasMutableDescriptorTypeExtension = true,
				.m_MutableDescriptorType = true,
				.m_DescriptorCapacityLimits = {
					.m_MaxDescriptorSetUpdateAfterBindSampledImages = resourceCount,
					.m_MaxPerStageDescriptorUpdateAfterBindSampledImages = resourceCount,
					.m_MaxDescriptorSetUpdateAfterBindStorageImages = resourceCount,
					.m_MaxPerStageDescriptorUpdateAfterBindStorageImages = resourceCount,
					.m_MaxDescriptorSetUpdateAfterBindSamplers = samplerCount,
					.m_MaxPerStageDescriptorUpdateAfterBindSamplers = samplerCount,
					.m_MaxPerStageUpdateAfterBindResources = combinedCount,
					.m_MaxUpdateAfterBindDescriptorsInAllPools = combinedCount,
				},
				.m_GlobalDescriptorSetLayoutSupported = true,
				.m_RequiredFormatFeaturesSupported = true,
			};
		}

		void RunDescriptorCapacityTests(SelfTestContext& context) noexcept
		{
			context.Check(GGLabDescriptorCapacityContract.m_ResourceDescriptorCount == 65'536,
				"Resource descriptor capacity is fixed at 65,536");
			context.Check(GGLabDescriptorCapacityContract.m_SamplerDescriptorCount == 2'048,
				"Sampler descriptor capacity is fixed at 2,048");
			context.Check(GGLabVulkanShaderBindingABI.m_DescriptorCapacity.m_ResourceDescriptorCount ==
				GGLabDescriptorCapacityContract.m_ResourceDescriptorCount &&
				GGLabVulkanShaderBindingABI.m_DescriptorCapacity.m_SamplerDescriptorCount ==
				GGLabDescriptorCapacityContract.m_SamplerDescriptorCount,
				"Vulkan binding ABI consumes the backend-neutral descriptor capacity contract");
		}

		void RunShaderBindingABITests(SelfTestContext& context) noexcept
		{
			context.Check(GGLabVulkanShaderBindingABI.m_Revision == 1,
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
						registerIndex, GGLabVulkanShaderBindingABI.m_FixedHlslRegisterSpace);
					const uint32_t expectedBinding = range.m_BindingShift + registerIndex;
					allBindingsUnique &= result.IsSupported() &&
						result.m_Location.m_DescriptorSet ==
						GGLabVulkanShaderBindingABI.m_FixedDescriptorSet &&
						result.m_Location.m_Binding == expectedBinding &&
						expectedBinding < occupiedBindings.size() && !occupiedBindings[expectedBinding];
					if (expectedBinding < occupiedBindings.size())
					{
						occupiedBindings[expectedBinding] = true;
					}
				}
				const auto outOfRange = EvaluateVulkanFixedShaderBinding(testCase.m_RegisterClass,
					range.m_RegisterCount, GGLabVulkanShaderBindingABI.m_FixedHlslRegisterSpace);
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
				GGLabVulkanShaderBindingABI.m_GlobalHeapHlslRegisterSpace);
			context.Check(!reservedSpace.IsSupported() && reservedSpace.m_RejectionReason ==
				VulkanShaderBindingRejectionReason::ReservedGlobalHeapRegisterSpace,
				"Fixed bindings reject HLSL space1 reserved for global heaps");
			const auto unsupportedSpace = EvaluateVulkanFixedShaderBinding(
				VulkanShaderRegisterClass::ShaderResource, 0,
				GGLabVulkanShaderBindingABI.m_GlobalHeapHlslRegisterSpace + 1);
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

		void RunDeviceProfileTests(SelfTestContext& context) noexcept
		{
			const VulkanDeviceProfileCapabilities supported = MakeSupportedCapabilities();
			context.Check(EvaluateVulkanDeviceProfile(supported).IsAccepted(),
				"Vulkan profile accepts its exact minimum required capabilities");
			context.Check(!supported.m_DescriptorBindingVariableDescriptorCount &&
				!supported.m_DescriptorBindingUniformBufferUpdateAfterBind &&
				!supported.m_DescriptorBindingStorageBufferUpdateAfterBind &&
				!supported.m_ShaderUniformBufferArrayNonUniformIndexing &&
				!supported.m_ShaderStorageBufferArrayNonUniformIndexing &&
				EvaluateVulkanDeviceProfile(supported).IsAccepted(),
				"Vulkan profile does not require variable-count or bindless-buffer features");

			auto multipleMissing = supported;
			multipleMissing.m_DynamicRendering = false;
			multipleMissing.m_SamplerAnisotropy = false;
			const auto multipleMissingEvaluation = EvaluateVulkanDeviceProfile(multipleMissing);
			context.Check(multipleMissingEvaluation.m_RejectionReasonCount == 2 &&
				multipleMissingEvaluation.HasReason(
					VulkanDeviceProfileRejectionReason::DynamicRenderingUnavailable) &&
				multipleMissingEvaluation.HasReason(
					VulkanDeviceProfileRejectionReason::SamplerAnisotropyUnavailable),
				"Vulkan profile reports every missing requirement in one evaluation");

			struct RequiredBooleanCase
			{
				bool VulkanDeviceProfileCapabilities::* m_Field;
				VulkanDeviceProfileRejectionReason m_Reason;
				std::string_view m_CheckName;
			};
			constexpr std::array RequiredBooleanCases{
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_IsWindowsX64,
					VulkanDeviceProfileRejectionReason::UnsupportedPlatform,
					"Vulkan profile reports unsupported platform" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_HasVulkanLoader,
					VulkanDeviceProfileRejectionReason::VulkanLoaderUnavailable,
					"Vulkan profile reports a missing loader" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_HasWin32SurfaceExtension,
					VulkanDeviceProfileRejectionReason::Win32SurfaceExtensionUnavailable,
					"Vulkan profile reports a missing Win32 surface extension" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_HasSwapchainExtension,
					VulkanDeviceProfileRejectionReason::SwapchainExtensionUnavailable,
					"Vulkan profile reports a missing swapchain extension" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_HasGraphicsPresentQueue,
					VulkanDeviceProfileRejectionReason::GraphicsPresentQueueUnavailable,
					"Vulkan profile reports a missing graphics-present queue" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_DynamicRendering,
					VulkanDeviceProfileRejectionReason::DynamicRenderingUnavailable,
					"Vulkan profile reports missing dynamic rendering" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_Synchronization2,
					VulkanDeviceProfileRejectionReason::Synchronization2Unavailable,
					"Vulkan profile reports missing synchronization2" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_TimelineSemaphore,
					VulkanDeviceProfileRejectionReason::TimelineSemaphoreUnavailable,
					"Vulkan profile reports missing timeline semaphores" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_ScalarBlockLayout,
					VulkanDeviceProfileRejectionReason::ScalarBlockLayoutUnavailable,
					"Vulkan profile reports missing scalar block layout" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_SamplerAnisotropy,
					VulkanDeviceProfileRejectionReason::SamplerAnisotropyUnavailable,
					"Vulkan profile reports missing sampler anisotropy" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_ShaderStorageImageExtendedFormats,
					VulkanDeviceProfileRejectionReason::ShaderStorageImageExtendedFormatsUnavailable,
					"Vulkan profile reports missing storage-image extended formats" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_RuntimeDescriptorArray,
					VulkanDeviceProfileRejectionReason::RuntimeDescriptorArrayUnavailable,
					"Vulkan profile reports missing runtime descriptor arrays" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_DescriptorBindingPartiallyBound,
					VulkanDeviceProfileRejectionReason::DescriptorBindingPartiallyBoundUnavailable,
					"Vulkan profile reports missing partially-bound descriptors" },
				RequiredBooleanCase{
					&VulkanDeviceProfileCapabilities::m_DescriptorBindingUpdateUnusedWhilePending,
					VulkanDeviceProfileRejectionReason::
						DescriptorBindingUpdateUnusedWhilePendingUnavailable,
					"Vulkan profile reports missing update-unused-while-pending" },
				RequiredBooleanCase{
					&VulkanDeviceProfileCapabilities::m_DescriptorBindingSampledImageUpdateAfterBind,
					VulkanDeviceProfileRejectionReason::
						DescriptorBindingSampledImageUpdateAfterBindUnavailable,
					"Vulkan profile reports missing sampled-image update-after-bind" },
				RequiredBooleanCase{
					&VulkanDeviceProfileCapabilities::m_DescriptorBindingStorageImageUpdateAfterBind,
					VulkanDeviceProfileRejectionReason::
						DescriptorBindingStorageImageUpdateAfterBindUnavailable,
					"Vulkan profile reports missing storage-image update-after-bind" },
				RequiredBooleanCase{
					&VulkanDeviceProfileCapabilities::m_ShaderSampledImageArrayNonUniformIndexing,
					VulkanDeviceProfileRejectionReason::
						ShaderSampledImageArrayNonUniformIndexingUnavailable,
					"Vulkan profile reports missing sampled-image non-uniform indexing" },
				RequiredBooleanCase{
					&VulkanDeviceProfileCapabilities::m_ShaderStorageImageArrayNonUniformIndexing,
					VulkanDeviceProfileRejectionReason::
						ShaderStorageImageArrayNonUniformIndexingUnavailable,
					"Vulkan profile reports missing storage-image non-uniform indexing" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_HasMutableDescriptorTypeExtension,
					VulkanDeviceProfileRejectionReason::MutableDescriptorTypeExtensionUnavailable,
					"Vulkan profile reports a missing mutable descriptor extension" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_MutableDescriptorType,
					VulkanDeviceProfileRejectionReason::MutableDescriptorTypeUnavailable,
					"Vulkan profile reports a missing mutable descriptor feature" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_GlobalDescriptorSetLayoutSupported,
					VulkanDeviceProfileRejectionReason::GlobalDescriptorSetLayoutUnsupported,
					"Vulkan profile reports an unsupported global descriptor-set layout" },
				RequiredBooleanCase{ &VulkanDeviceProfileCapabilities::m_RequiredFormatFeaturesSupported,
					VulkanDeviceProfileRejectionReason::RequiredFormatFeaturesUnavailable,
					"Vulkan profile reports missing rendering format features" },
			};

			for (const auto& testCase : RequiredBooleanCases)
			{
				auto missing = supported;
				missing.*testCase.m_Field = false;
				const auto evaluation = EvaluateVulkanDeviceProfile(missing);
				context.Check(!evaluation.IsAccepted() && evaluation.m_RejectionReasonCount == 1 &&
					evaluation.HasReason(testCase.m_Reason), testCase.m_CheckName);
			}

			auto oldApi = supported;
			oldApi.m_ApiVersion = { 1, 2 };
			const auto oldApiEvaluation = EvaluateVulkanDeviceProfile(oldApi);
			context.Check(oldApiEvaluation.m_RejectionReasonCount == 1 && oldApiEvaluation.HasReason(
				VulkanDeviceProfileRejectionReason::ApiVersionTooLow),
				"Vulkan profile rejects API versions below 1.3 explicitly");

			struct CapacityLimitCase
			{
				uint32_t VulkanDescriptorCapacityLimits::* m_Field;
				uint32_t m_RequiredValue;
				VulkanDeviceProfileRejectionReason m_Reason;
				std::string_view m_CheckName;
			};
			const uint32_t resourceCount =
				GGLabDescriptorCapacityContract.m_ResourceDescriptorCount;
			const uint32_t samplerCount =
				GGLabDescriptorCapacityContract.m_SamplerDescriptorCount;
			const uint32_t combinedCount = resourceCount + samplerCount;
			const std::array CapacityLimitCases{
				CapacityLimitCase{
					&VulkanDescriptorCapacityLimits::m_MaxDescriptorSetUpdateAfterBindSampledImages,
					resourceCount,
					VulkanDeviceProfileRejectionReason::DescriptorSetSampledImageLimitInsufficient,
					"Vulkan profile checks the descriptor-set sampled-image limit" },
				CapacityLimitCase{
					&VulkanDescriptorCapacityLimits::m_MaxPerStageDescriptorUpdateAfterBindSampledImages,
					resourceCount,
					VulkanDeviceProfileRejectionReason::PerStageSampledImageLimitInsufficient,
					"Vulkan profile checks the per-stage sampled-image limit" },
				CapacityLimitCase{
					&VulkanDescriptorCapacityLimits::m_MaxDescriptorSetUpdateAfterBindStorageImages,
					resourceCount,
					VulkanDeviceProfileRejectionReason::DescriptorSetStorageImageLimitInsufficient,
					"Vulkan profile checks the descriptor-set storage-image limit" },
				CapacityLimitCase{
					&VulkanDescriptorCapacityLimits::m_MaxPerStageDescriptorUpdateAfterBindStorageImages,
					resourceCount,
					VulkanDeviceProfileRejectionReason::PerStageStorageImageLimitInsufficient,
					"Vulkan profile checks the per-stage storage-image limit" },
				CapacityLimitCase{
					&VulkanDescriptorCapacityLimits::m_MaxDescriptorSetUpdateAfterBindSamplers,
					samplerCount,
					VulkanDeviceProfileRejectionReason::DescriptorSetSamplerLimitInsufficient,
					"Vulkan profile checks the descriptor-set sampler limit" },
				CapacityLimitCase{
					&VulkanDescriptorCapacityLimits::m_MaxPerStageDescriptorUpdateAfterBindSamplers,
					samplerCount,
					VulkanDeviceProfileRejectionReason::PerStageSamplerLimitInsufficient,
					"Vulkan profile checks the per-stage sampler limit" },
				CapacityLimitCase{
					&VulkanDescriptorCapacityLimits::m_MaxPerStageUpdateAfterBindResources,
					combinedCount,
					VulkanDeviceProfileRejectionReason::
						PerStageUpdateAfterBindResourceLimitInsufficient,
					"Vulkan profile checks the combined per-stage update-after-bind limit" },
				CapacityLimitCase{
					&VulkanDescriptorCapacityLimits::m_MaxUpdateAfterBindDescriptorsInAllPools,
					combinedCount,
					VulkanDeviceProfileRejectionReason::UpdateAfterBindPoolLimitInsufficient,
					"Vulkan profile checks the update-after-bind pool limit" },
			};
			for (const auto& testCase : CapacityLimitCases)
			{
				auto insufficient = supported;
				insufficient.m_DescriptorCapacityLimits.*testCase.m_Field = testCase.m_RequiredValue - 1;
				const auto evaluation = EvaluateVulkanDeviceProfile(insufficient);
				context.Check(evaluation.m_RejectionReasonCount == 1 &&
					evaluation.HasReason(testCase.m_Reason), testCase.m_CheckName);
			}

			auto asymmetricLimits = supported.m_DescriptorCapacityLimits;
			asymmetricLimits.m_MaxPerStageDescriptorUpdateAfterBindStorageImages = resourceCount - 7;
			asymmetricLimits.m_MaxPerStageUpdateAfterBindResources = combinedCount - 3;
			const auto availability =
				CalculateVulkanDescriptorCapacityAvailability(asymmetricLimits);
			context.Check(availability.m_ResourceDescriptorCount == resourceCount - 7 &&
				availability.m_SamplerDescriptorCount == samplerCount &&
				availability.m_CombinedDescriptorCount == combinedCount - 3,
				"Vulkan descriptor capacity is the minimum across every relevant native limit");
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
			context.Check(GGLabVulkanCoordinatePolicy.m_UsePositiveViewportHeight &&
				GGLabVulkanCoordinatePolicy.m_InvertVertexProducingStageY &&
				GGLabVulkanCoordinatePolicy.m_UseDxPositionW &&
				!GGLabVulkanCoordinatePolicy.m_BackendAppliesAdditionalReversedZ,
				"Vulkan coordinate lowering applies each required correction exactly once");
		}

		void RunVulkanBarrierContractTests(SelfTestContext& context) noexcept
		{
			const VkPipelineStageFlags2 shaderStages =
				ToVulkanPipelineStages(RHIStage::VertexShader | RHIStage::PixelShader);
			context.Check(shaderStages == (VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT),
				"Vulkan barrier lowering preserves combined shader stages");

			const VkAccessFlags2 transferAccess =
				ToVulkanAccessFlags(RHIAccess::CopySource | RHIAccess::CopyDest);
			context.Check(transferAccess ==
				(VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT),
				"Vulkan barrier lowering preserves combined transfer access");

			context.Check(
				ToVulkanImageLayout(RHILayout::Undefined) == VK_IMAGE_LAYOUT_UNDEFINED &&
				ToVulkanImageLayout(RHILayout::CopyDest) ==
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
				ToVulkanImageLayout(RHILayout::RenderTarget) ==
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
				ToVulkanImageLayout(RHILayout::Present) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				"Vulkan barrier lowering maps transfer, attachment, and presentation layouts");
			context.Check(!ToVulkanImageLayout(RHILayout::Unknown).has_value(),
				"Vulkan barrier lowering rejects an unknown image layout");

			const VkImage image = reinterpret_cast<VkImage>(static_cast<uintptr_t>(17));
			const VkImageMemoryBarrier2 imageBarrier = MakeVulkanImageBarrier(image,
				VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE,
				VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
			context.Check(imageBarrier.image == image &&
				imageBarrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
				imageBarrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
				imageBarrier.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
				imageBarrier.subresourceRange.levelCount == 1 &&
				imageBarrier.subresourceRange.layerCount == 1,
				"Vulkan image barriers use explicit synchronization and singular ranges");
			const VkImageSubresourceRange range{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 2,
				.levelCount = 3,
				.baseArrayLayer = 4,
				.layerCount = 5,
			};
			const VkImageMemoryBarrier2 rangedBarrier = MakeVulkanImageBarrier(image, range,
				VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
			context.Check(rangedBarrier.subresourceRange.aspectMask == range.aspectMask &&
				rangedBarrier.subresourceRange.baseMipLevel == range.baseMipLevel &&
				rangedBarrier.subresourceRange.levelCount == range.levelCount &&
				rangedBarrier.subresourceRange.baseArrayLayer == range.baseArrayLayer &&
				rangedBarrier.subresourceRange.layerCount == range.layerCount,
				"Vulkan image barrier construction preserves explicit subresource ranges");
		}

		void RunVulkanTextureCopyContractTests(SelfTestContext& context) noexcept
		{
			const RHITextureDesc desc{
				.m_Format = RHIFormat::R8G8B8A8Unorm,
				.m_Extent = { 4, 4, 1 },
				.m_ArraySize = 2,
				.m_MipLevels = 2,
			};
			const auto layout = BuildVulkanTextureCopyLayout(desc, 16);
			const bool regionsAligned = layout && std::ranges::all_of(layout->m_Regions,
				[](const VkBufferImageCopy2& region) noexcept
				{
					return region.bufferOffset % 16 == 0;
				});
			context.Check(layout && layout->m_TotalBytes == 160 &&
				layout->m_Regions.size() == 4 && layout->m_Subresources.size() == 4 &&
				regionsAligned,
				"Vulkan texture copy layout covers every mip and array layer with aligned offsets");

			context.Check(
				ToVulkanImageType(RHITextureDimension::Texture1D) == VK_IMAGE_TYPE_1D &&
				ToVulkanImageType(RHITextureDimension::Texture2D) == VK_IMAGE_TYPE_2D &&
				ToVulkanImageType(RHITextureDimension::Texture3D) == VK_IMAGE_TYPE_3D &&
				ToVulkanSampleCount(4) == VK_SAMPLE_COUNT_4_BIT,
				"Vulkan resource conversions use the shared format helpers");
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
			ShaderCompiler compiler;
			compiler.SetCacheRootDirectory(scopedDirectory.GetPath());
			const SpirVValidatorInfo validator = FindSpirVValidator();
			context.Check(validator.MatchesValidationBaseline(),
				"Vulkan SDK and SPIR-V Tools match the configured validation baseline");

			ShaderDesc coverageDesc{
				.m_SourcePath = L"Passes/PassForwardCoverage.hlsl",
				.m_Stage = ShaderStage::Vertex,
				.m_Entry = L"VSMain",
				.m_IncludeDirs = {L"."},
			};
			const ShaderDesc normalizedDxil = compiler.NormalizeShaderDesc(coverageDesc);
			const ShaderCompileArtifact dxilArtifact =
				compiler.CompileOrLoadArtifact(normalizedDxil);

			coverageDesc.m_Target = ShaderCompiler::MakeVulkanSpirVTarget(coverageDesc.m_Stage);
			const ShaderDesc normalizedSpirV = compiler.NormalizeShaderDesc(coverageDesc);
			const ShaderCompileArtifact spirVArtifact =
				compiler.CompileOrLoadArtifact(normalizedSpirV);
			const ShaderCompileArtifact cachedSpirVArtifact =
				compiler.CompileOrLoadArtifact(normalizedSpirV);
			const ShaderCompileValidationResult dxilValidation =
				ValidateShaderDesc(normalizedDxil, compiler.GetCompilerVersion());
			const ShaderCompileValidationResult spirVValidation =
				ValidateShaderDesc(normalizedSpirV, compiler.GetCompilerVersion());

			const std::wstring dxilPath = dxilArtifact.m_BinaryPath.generic_wstring();
			const std::wstring spirVPath = spirVArtifact.m_BinaryPath.generic_wstring();
			context.Check(dxilValidation.IsValid() && spirVValidation.IsValid() &&
				dxilArtifact.m_Binary.IsValid() && spirVArtifact.m_Binary.IsValid() &&
				dxilArtifact.GetBinaryFormat() == ShaderBinaryFormat::Dxil &&
				spirVArtifact.GetBinaryFormat() == ShaderBinaryFormat::SpirV,
				"One normalized HLSL recipe produces valid DXIL and SPIR-V artifacts");
			context.Check(dxilPath.find(L"/dxil/") != std::wstring::npos &&
				spirVPath.find(L"/spirv/") != std::wstring::npos &&
				dxilArtifact.m_BinaryPath.extension() == L".dxil" &&
				spirVArtifact.m_BinaryPath.extension() == L".spv" &&
				dxilArtifact.m_BinaryPath != spirVArtifact.m_BinaryPath,
				"Shader cache partitions DXIL and SPIR-V by directory and extension");
			context.Check(!spirVArtifact.m_FromCache && cachedSpirVArtifact.m_FromCache,
				"SPIR-V artifact cache reuses an exact normalized target");
			const bool cacheBlobOverwritten =
				OverwriteBinaryFile(spirVArtifact.m_BinaryPath, dxilArtifact.m_Binary);
			const ShaderCompileArtifact recoveredSpirVArtifact =
				compiler.CompileOrLoadArtifact(normalizedSpirV);
			SpirVDecorationReflection recoveredReflection;
			context.Check(cacheBlobOverwritten && !recoveredSpirVArtifact.m_FromCache &&
				ReadSpirVDecorations(recoveredSpirVArtifact.m_Binary, recoveredReflection),
				"Shader cache rejects a blob whose actual format disagrees with its target metadata");
			context.Check(normalizedSpirV.m_Target.m_DxcVersion == compiler.GetCompilerVersion() &&
				!compiler.GetCompilerVersion().empty() && compiler.GetCompilerVersion() != L"unknown",
				"Normalized shader target records the concrete DXC compiler identity");

			ShaderDesc managerDesc{
				.m_SourcePath = L"Passes/PassForwardCoverage.hlsl",
				.m_Stage = ShaderStage::Vertex,
				.m_Target = ShaderCompiler::MakeVulkanSpirVTarget(ShaderStage::Vertex),
				.m_Entry = L"VSMain",
			};
			ShaderManager dxilManager(RHIBackendType::DX12);
			const ShaderID dxilManagerShader = dxilManager.LoadShader(managerDesc);
			managerDesc.m_Target = {};
			ShaderManager spirVManager(RHIBackendType::Vulkan);
			const ShaderID spirVManagerShader = spirVManager.LoadShader(managerDesc);
			context.Check(dxilManager.GetActiveBackend() == RHIBackendType::DX12 &&
				spirVManager.GetActiveBackend() == RHIBackendType::Vulkan &&
				dxilManagerShader.IsValid() && spirVManagerShader.IsValid() &&
				dxilManager.GetBytecode(dxilManagerShader).m_Format == ShaderBinaryFormat::Dxil &&
				spirVManager.GetBytecode(spirVManagerShader).m_Format == ShaderBinaryFormat::SpirV,
				"ShaderManager derives shader format from its active RHI backend");

			const ShaderHash128 dxilRecipe = ShaderCompiler::ComputeRecipeHash(normalizedDxil);
			const ShaderHash128 spirVRecipe = ShaderCompiler::ComputeRecipeHash(normalizedSpirV);
			auto changedABI = normalizedSpirV;
			++changedABI.m_Target.m_BindingABIRevision;
			auto changedDxc = normalizedSpirV;
			changedDxc.m_Target.m_DxcVersion += L"-different";
			auto changedCoordinates = normalizedSpirV;
			changedCoordinates.m_Target.m_CoordinateOptions = ShaderCoordinateOptions::None;
			auto changedArguments = normalizedSpirV;
			changedArguments.m_ExtraArgs.push_back(L"-GGLAB_TEST_ARGUMENT");
			context.Check(dxilRecipe != spirVRecipe && spirVRecipe !=
				ShaderCompiler::ComputeRecipeHash(changedABI) && spirVRecipe !=
				ShaderCompiler::ComputeRecipeHash(changedDxc) && spirVRecipe !=
				ShaderCompiler::ComputeRecipeHash(changedCoordinates) && spirVRecipe !=
				ShaderCompiler::ComputeRecipeHash(changedArguments),
				"Shader recipe identity includes format, ABI, DXC, coordinates, and compile arguments");

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
				auto bypassDesc = normalizedSpirV;
				bypassDesc.m_ExtraArgs = { std::wstring(argument) };
				const ShaderCompileValidationResult result =
					ValidateShaderDesc(bypassDesc, compiler.GetCompilerVersion());
				allReservedArgumentsRejected &= !result.IsValid() &&
					result.m_Error == ShaderCompileValidationError::ReservedExtraArgument;
			}
			context.Check(allReservedArgumentsRejected,
				"Shader validation prevents extra arguments from overriding normalized target options");

			auto mismatchedDxcDesc = normalizedSpirV;
			mismatchedDxcDesc.m_Target.m_DxcVersion += L"-mismatch";
			auto mismatchedAbiDesc = normalizedSpirV;
			++mismatchedAbiDesc.m_Target.m_BindingABIRevision;
			auto mismatchedCoordinatesDesc = normalizedSpirV;
			mismatchedCoordinatesDesc.m_Target.m_CoordinateOptions = ShaderCoordinateOptions::None;
			const ShaderCompileValidationResult dxcError =
				ValidateShaderDesc(mismatchedDxcDesc, compiler.GetCompilerVersion());
			const ShaderCompileValidationResult abiError =
				ValidateShaderDesc(mismatchedAbiDesc, compiler.GetCompilerVersion());
			const ShaderCompileValidationResult coordinateError =
				ValidateShaderDesc(mismatchedCoordinatesDesc, compiler.GetCompilerVersion());
			context.Check(dxcError.m_Error == ShaderCompileValidationError::CompilerIdentityMismatch &&
				abiError.m_Error == ShaderCompileValidationError::UnsupportedBindingABIRevision &&
				coordinateError.m_Error == ShaderCompileValidationError::InvalidCoordinateOptions,
				"Shader validation reports structured DXC, binding ABI, and coordinate errors");

			auto rejectedCompileDesc = normalizedSpirV;
			rejectedCompileDesc.m_ExtraArgs = { L"-fspv-target-env=vulkan1.0" };
			const ShaderCompileArtifact rejectedCompileArtifact =
				compiler.CompileOrLoadArtifact(rejectedCompileDesc);
			context.Check(!rejectedCompileArtifact.m_Binary.IsValid(),
				"Shader compiler rejects invalid target contracts without relying on assertions");

			const std::vector<std::wstring> vertexArguments =
				ShaderCompiler::BuildCompileArguments(normalizedSpirV);
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
					std::to_wstring(GGLabVulkanShaderBindingABI.m_FixedHlslRegisterSpace);
				registerShiftsMatch &=
					ContainsArgumentSequence(vertexArguments, { option, shift, hlslSpace });
			}
			const std::wstring resourceBinding =
				std::to_wstring(GGLabVulkanShaderBindingABI.m_ResourceHeapBinding);
			const std::wstring samplerBinding =
				std::to_wstring(GGLabVulkanShaderBindingABI.m_SamplerHeapBinding);
			const std::wstring descriptorSet =
				std::to_wstring(GGLabVulkanShaderBindingABI.m_GlobalDescriptorSet);
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
				.m_Target = ShaderCompiler::MakeVulkanSpirVTarget(ShaderStage::Pixel),
				.m_Entry = L"PSMain",
				.m_IncludeDirs = {L"."},
			};
			const ShaderDesc normalizedForwardPixel = compiler.NormalizeShaderDesc(forwardPixelDesc);
			const auto pixelArguments =
				ShaderCompiler::BuildCompileArguments(normalizedForwardPixel);
			context.Check(ContainsArgument(pixelArguments, L"-fvk-use-dx-position-w") &&
				!ContainsArgument(pixelArguments, L"-fvk-invert-y"),
				"Pixel SPIR-V compile policy preserves the HLSL SV_Position.w contract");

			std::ifstream metadataInput(spirVArtifact.m_MetaPath, std::ios::binary);
			const std::string metadata((std::istreambuf_iterator<char>(metadataInput)),
				std::istreambuf_iterator<char>());
			context.Check(metadata.find("schema=2") != std::string::npos &&
				metadata.find("binary_format=spirv") != std::string::npos &&
				metadata.find("target_environment=vulkan1.3") != std::string::npos &&
				metadata.find("binding_abi_revision=1") != std::string::npos &&
				metadata.find("dxc_version=") != std::string::npos,
				"Shader artifact metadata records target format, environment, ABI, and DXC identity");
			const bool metadataChanged = ReplaceMetadataValue(
				spirVArtifact.m_MetaPath, "binding_abi_revision", "999");
			const ShaderCompileArtifact rejectedCacheArtifact =
				compiler.CompileOrLoadArtifact(normalizedSpirV);
			context.Check(metadataChanged && !rejectedCacheArtifact.m_FromCache,
				"Shader cache rejects metadata whose target contract does not match the recipe");

			SpirVDecorationReflection coverageReflection;
			const bool coverageReflected =
				ReadSpirVDecorations(spirVArtifact.m_Binary, coverageReflection);
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

			const ShaderCompileArtifact forwardPixelArtifact =
				compiler.CompileOrLoadArtifact(normalizedForwardPixel);
			SpirVDecorationReflection forwardPixelReflection;
			const bool forwardPixelReflected =
				ReadSpirVDecorations(forwardPixelArtifact.m_Binary, forwardPixelReflection);
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
				.m_Target = ShaderCompiler::MakeVulkanSpirVTarget(ShaderStage::Compute),
				.m_Entry = L"CSMain",
				.m_IncludeDirs = {L"."},
			};
			const ShaderCompileArtifact cullArtifact =
				compiler.CompileOrLoadArtifact(compiler.NormalizeShaderDesc(cullDesc));
			SpirVDecorationReflection cullReflection;
			const bool cullReflected = ReadSpirVDecorations(cullArtifact.m_Binary, cullReflection);
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
				.m_Target = ShaderCompiler::MakeVulkanSpirVTarget(ShaderStage::Compute),
				.m_Entry = L"CSMain",
				.m_IncludeDirs = {L"."},
			};
			const ShaderCompileArtifact gtaoArtifact =
				compiler.CompileOrLoadArtifact(compiler.NormalizeShaderDesc(gtaoDesc));
			gtaoDesc.m_Defines = {
				{.m_Name = L"GGLAB_GTAO_DIAGNOSTICS", .m_Value = L"1"},
			};
			const ShaderCompileArtifact gtaoDiagnosticsArtifact =
				compiler.CompileOrLoadArtifact(compiler.NormalizeShaderDesc(gtaoDesc));

			ShaderDesc storageDesc{
				.m_SourcePath = L"Passes/PassRenderGraphComputeSmoke.hlsl",
				.m_Stage = ShaderStage::Compute,
				.m_Target = ShaderCompiler::MakeVulkanSpirVTarget(ShaderStage::Compute),
				.m_Entry = L"CSWrite",
				.m_IncludeDirs = {L"."},
			};
			const ShaderCompileArtifact storageArtifact =
				compiler.CompileOrLoadArtifact(compiler.NormalizeShaderDesc(storageDesc));
			SpirVDecorationReflection storageReflection;
			const bool storageReflected =
				ReadSpirVDecorations(storageArtifact.m_Binary, storageReflection);
			context.Check(storageReflected && HasDescriptorBinding(storageReflection, 0, 2) &&
				HasDescriptorBinding(storageReflection, 1, 0),
				"RenderGraph storage-write SPIR-V maps fixed constants and the mutable resource heap");

			ShaderDesc fullscreenDesc = storageDesc;
			fullscreenDesc.m_Stage = ShaderStage::Vertex;
			fullscreenDesc.m_Target = ShaderCompiler::MakeVulkanSpirVTarget(ShaderStage::Vertex);
			fullscreenDesc.m_Entry = L"VSMain";
			const ShaderCompileArtifact fullscreenArtifact =
				compiler.CompileOrLoadArtifact(compiler.NormalizeShaderDesc(fullscreenDesc));
			SpirVDecorationReflection fullscreenReflection;
			const bool fullscreenReflected =
				ReadSpirVDecorations(fullscreenArtifact.m_Binary, fullscreenReflection);
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

			const std::array artifactsToValidate{
				&spirVArtifact,
				&forwardPixelArtifact,
				&cullArtifact,
				&gtaoArtifact,
				&gtaoDiagnosticsArtifact,
				&storageArtifact,
				&fullscreenArtifact,
			};
			const bool allValidated = validator.MatchesValidationBaseline() &&
				std::ranges::all_of(artifactsToValidate,
					[&validator](const ShaderCompileArtifact* artifact) noexcept
					{
						return artifact->m_Binary.IsValid() &&
							ValidateSpirVBinary(validator.m_Path, artifact->m_BinaryPath);
					});
			context.Check(allValidated,
				"Baseline spirv-val accepts representative vertex, pixel, compute, GTAO, and storage artifacts");
		}

#if GGLAB_ENABLE_VULKAN
		[[nodiscard]] VulkanDeviceProfileCapabilities MakeSatisfiedProfileCapabilities() noexcept
		{
			VulkanDeviceProfileCapabilities caps{};
			caps.m_IsWindowsX64 = true;
			caps.m_HasVulkanLoader = true;
			caps.m_ApiVersion = { 1, 3 };
			caps.m_HasWin32SurfaceExtension = true;
			caps.m_HasSwapchainExtension = true;
			caps.m_HasGraphicsPresentQueue = true;
			caps.m_DynamicRendering = true;
			caps.m_Synchronization2 = true;
			caps.m_TimelineSemaphore = true;
			caps.m_ScalarBlockLayout = true;
			caps.m_SamplerAnisotropy = true;
			caps.m_ShaderStorageImageExtendedFormats = true;
			caps.m_RuntimeDescriptorArray = true;
			caps.m_DescriptorBindingPartiallyBound = true;
			caps.m_DescriptorBindingUpdateUnusedWhilePending = true;
			caps.m_DescriptorBindingSampledImageUpdateAfterBind = true;
			caps.m_DescriptorBindingStorageImageUpdateAfterBind = true;
			caps.m_ShaderSampledImageArrayNonUniformIndexing = true;
			caps.m_ShaderStorageImageArrayNonUniformIndexing = true;
			caps.m_HasMutableDescriptorTypeExtension = true;
			caps.m_MutableDescriptorType = true;
			caps.m_DescriptorCapacityLimits = {
				.m_MaxDescriptorSetUpdateAfterBindSampledImages = 65'536,
				.m_MaxPerStageDescriptorUpdateAfterBindSampledImages = 65'536,
				.m_MaxDescriptorSetUpdateAfterBindStorageImages = 65'536,
				.m_MaxPerStageDescriptorUpdateAfterBindStorageImages = 65'536,
				.m_MaxDescriptorSetUpdateAfterBindSamplers = 2'048,
				.m_MaxPerStageDescriptorUpdateAfterBindSamplers = 2'048,
				.m_MaxPerStageUpdateAfterBindResources = 67'584,
				.m_MaxUpdateAfterBindDescriptorsInAllPools = 67'584,
			};
			caps.m_GlobalDescriptorSetLayoutSupported = true;
			caps.m_RequiredFormatFeaturesSupported = true;
			return caps;
		}

		[[nodiscard]] VulkanAdapterCapabilitySnapshot MakeAdapterSnapshot(uint32_t index,
			std::string name, VkPhysicalDeviceType type,
			const VulkanDeviceProfileCapabilities& capabilities) noexcept
		{
			VulkanAdapterCapabilitySnapshot snapshot{};
			snapshot.m_Identity.m_EnumerationIndex = index;
			snapshot.m_Identity.m_DeviceName = std::move(name);
			snapshot.m_Identity.m_DeviceType = type;
			for (uint32_t byte = 0; byte < snapshot.m_Identity.m_DeviceUuid.size(); ++byte)
			{
				snapshot.m_Identity.m_DeviceUuid[byte] =
					static_cast<uint8_t>((index + 1) * 17 + byte);
			}
			snapshot.m_ProfileCapabilities = capabilities;
			// A well-formed snapshot models an adapter whose layout probe ran.
			snapshot.m_GlobalDescriptorSetLayoutProbed = true;
			snapshot.m_ProfileEvaluation =
				EvaluateVulkanDeviceProfile(snapshot.m_ProfileCapabilities);
			return snapshot;
		}

		void RunVulkanBootstrapSelectionTests(SelfTestContext& context) noexcept
		{
			const VulkanDeviceProfileCapabilities satisfied = MakeSatisfiedProfileCapabilities();

			// Default selection prefers a discrete accepted adapter.
			{
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(MakeAdapterSnapshot(0, "Intel Integrated", VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, satisfied));
				snapshots.push_back(MakeAdapterSnapshot(1, "NVIDIA Discrete", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				const VulkanAdapterSelectionResult result =
					SelectVulkanAdapter(snapshots, {});
				context.Check(result.IsSelected() && result.m_SelectedIndex == 1,
					"Default adapter selection prefers a discrete GPU over integrated");
			}
			// Default selection is stable by enumeration index within one rank.
			{
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(MakeAdapterSnapshot(0, "NVIDIA First", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				snapshots.push_back(MakeAdapterSnapshot(1, "NVIDIA Second", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				const VulkanAdapterSelectionResult result =
					SelectVulkanAdapter(snapshots, {});
				context.Check(result.IsSelected() && result.m_SelectedIndex == 0,
					"Default adapter selection is deterministic by enumeration index");
			}
			// Explicit index selects the requested adapter.
			{
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(MakeAdapterSnapshot(0, "First", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				snapshots.push_back(MakeAdapterSnapshot(1, "Second", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				const VulkanAdapterSelectionResult result =
					SelectVulkanAdapter(snapshots, { .m_Kind = VulkanAdapterSelectionKind::Index, .m_Index = 1 });
				context.Check(result.IsSelected() && result.m_SelectedIndex == 1,
					"Explicit adapter index selects the requested adapter");
			}
			// Explicit index of a rejected adapter reports the rejection.
			{
				auto rejectedCapabilities = satisfied;
				rejectedCapabilities.m_DescriptorBindingUpdateUnusedWhilePending = false;
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(MakeAdapterSnapshot(0, "Rejected", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, rejectedCapabilities));
				// The rejected snapshot models a completed layout determination.
				snapshots[0].m_GlobalDescriptorSetLayoutProbed = true;
				const VulkanAdapterSelectionResult result =
					SelectVulkanAdapter(snapshots, { .m_Kind = VulkanAdapterSelectionKind::Index, .m_Index = 0 });
				context.Check(result.m_Status == VulkanAdapterSelectionStatus::RejectedAdapter &&
					snapshots[0].m_ProfileEvaluation.HasReason(
						VulkanDeviceProfileRejectionReason::DescriptorBindingUpdateUnusedWhilePendingUnavailable),
					"Explicit selection of a rejected adapter reports its rejection reason");
			}
			// Index out of range fails without fallback.
			{
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(MakeAdapterSnapshot(0, "Only", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				const VulkanAdapterSelectionResult result =
					SelectVulkanAdapter(snapshots, { .m_Kind = VulkanAdapterSelectionKind::Index, .m_Index = 7 });
				context.Check(result.m_Status == VulkanAdapterSelectionStatus::IndexOutOfRange,
					"Adapter index out of range fails without fallback");
			}
			// Unique identity prefix selects exactly one adapter.
			{
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(MakeAdapterSnapshot(0, "NVIDIA Alpha", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				snapshots.push_back(MakeAdapterSnapshot(1, "NVIDIA Beta", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				const std::string uuidPrefix =
					snapshots[1].m_Identity.UuidHex().substr(0, 6);
				const VulkanAdapterSelectionResult result =
					SelectVulkanAdapter(snapshots, { .m_Kind = VulkanAdapterSelectionKind::Prefix, .m_Prefix = uuidPrefix });
				context.Check(result.IsSelected() && result.m_SelectedIndex == 1,
					"Unique identity prefix selects exactly one adapter");
			}
			// Name prefix matches case-insensitively.
			{
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(MakeAdapterSnapshot(0, "NVIDIA GeForce RTX", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				const VulkanAdapterSelectionResult result =
					SelectVulkanAdapter(snapshots, { .m_Kind = VulkanAdapterSelectionKind::Prefix, .m_Prefix = "nvidia geforce" });
				context.Check(result.IsSelected() && result.m_SelectedIndex == 0,
					"Name prefix selection is case-insensitive");
			}
			// No match fails with an explicit status.
			{
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(MakeAdapterSnapshot(0, "NVIDIA", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				const VulkanAdapterSelectionResult result =
					SelectVulkanAdapter(snapshots, { .m_Kind = VulkanAdapterSelectionKind::Prefix, .m_Prefix = "amd" });
				context.Check(result.m_Status == VulkanAdapterSelectionStatus::SelectorNoMatch,
					"Adapter prefix with no match reports an explicit status");
			}
			// Ambiguous prefix fails.
			{
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(MakeAdapterSnapshot(0, "NVIDIA One", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				snapshots.push_back(MakeAdapterSnapshot(1, "NVIDIA Two", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				const VulkanAdapterSelectionResult result =
					SelectVulkanAdapter(snapshots, { .m_Kind = VulkanAdapterSelectionKind::Prefix, .m_Prefix = "nvidia" });
				context.Check(result.m_Status == VulkanAdapterSelectionStatus::SelectorAmbiguous,
					"Adapter prefix with multiple matches fails as ambiguous");
			}
			// No accepted adapters fails the default selection.
			{
				auto rejectedCapabilities = satisfied;
				rejectedCapabilities.m_ApiVersion = { 1, 2 };
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(MakeAdapterSnapshot(0, "Old API", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, rejectedCapabilities));
				// The rejected snapshot models a completed layout determination.
				snapshots[0].m_GlobalDescriptorSetLayoutProbed = true;
				const VulkanAdapterSelectionResult result = SelectVulkanAdapter(snapshots, {});
				context.Check(result.m_Status == VulkanAdapterSelectionStatus::NoAcceptedAdapter &&
					snapshots[0].m_ProfileEvaluation.HasReason(
						VulkanDeviceProfileRejectionReason::ApiVersionTooLow),
					"Default selection without accepted adapters reports the profile rejection");
			}
			// A rejected adapter preserves every missing capability reason.
			{
				auto degraded = satisfied;
				degraded.m_ApiVersion = { 1, 2 };
				degraded.m_HasGraphicsPresentQueue = false;
				degraded.m_HasSwapchainExtension = false;
				degraded.m_DescriptorBindingUpdateUnusedWhilePending = false;
				degraded.m_DescriptorCapacityLimits.m_MaxDescriptorSetUpdateAfterBindSampledImages = 1'024;
				degraded.m_DescriptorCapacityLimits.m_MaxPerStageDescriptorUpdateAfterBindSampledImages = 1'024;
				degraded.m_DescriptorCapacityLimits.m_MaxDescriptorSetUpdateAfterBindSamplers = 256;
				degraded.m_DescriptorCapacityLimits.m_MaxPerStageDescriptorUpdateAfterBindSamplers = 256;
				degraded.m_DescriptorCapacityLimits.m_MaxPerStageUpdateAfterBindResources = 1'024;
				degraded.m_DescriptorCapacityLimits.m_MaxUpdateAfterBindDescriptorsInAllPools = 1'024;
				degraded.m_GlobalDescriptorSetLayoutSupported = false;
				degraded.m_RequiredFormatFeaturesSupported = false;

				VulkanAdapterCapabilitySnapshot snapshot =
					MakeAdapterSnapshot(0, "Degraded", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, degraded);
				// The degraded snapshot models an adapter whose layout probe
				// ran and reported unsupported.
				snapshot.m_GlobalDescriptorSetLayoutProbed = true;
				const auto& evaluation = snapshot.m_ProfileEvaluation;
				context.Check(!evaluation.IsAccepted() &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::ApiVersionTooLow) &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::GraphicsPresentQueueUnavailable) &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::SwapchainExtensionUnavailable) &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::DescriptorBindingUpdateUnusedWhilePendingUnavailable) &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::DescriptorSetSampledImageLimitInsufficient) &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::PerStageSampledImageLimitInsufficient) &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::DescriptorSetSamplerLimitInsufficient) &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::PerStageSamplerLimitInsufficient) &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::PerStageUpdateAfterBindResourceLimitInsufficient) &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::UpdateAfterBindPoolLimitInsufficient) &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::GlobalDescriptorSetLayoutUnsupported) &&
					evaluation.HasReason(VulkanDeviceProfileRejectionReason::RequiredFormatFeaturesUnavailable),
					"A degraded adapter preserves every missing capability rejection reason");
			}
			// Not-probed must not report as unsupported: an adapter that fails
			// a non-layout requirement is rejected by the preliminary
			// evaluation without a layout reason.
			{
				auto missingTimeline = satisfied;
				missingTimeline.m_TimelineSemaphore = false;
				VulkanAdapterCapabilitySnapshot snapshot = MakeAdapterSnapshot(0, "MissingTimeline",
					VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, missingTimeline);
				const VulkanDeviceProfileEvaluation preliminary =
					EvaluateVulkanAdapterProfilePreliminary(snapshot);
				context.Check(!preliminary.IsAccepted() &&
					preliminary.HasReason(
						VulkanDeviceProfileRejectionReason::TimelineSemaphoreUnavailable) &&
					!preliminary.HasReason(
						VulkanDeviceProfileRejectionReason::GlobalDescriptorSetLayoutUnsupported),
					"Preliminary rejection never reports an unprobed layout as unsupported");
			}
			// All preliminary requirements pass and the layout probe reports
			// unsupported: the final evaluation carries the layout reason.
			{
				VulkanAdapterCapabilitySnapshot snapshot =
					MakeAdapterSnapshot(0, "LayoutUnsupported",
						VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied);
				const VulkanDeviceProfileEvaluation preliminary =
					EvaluateVulkanAdapterProfilePreliminary(snapshot);
				context.Check(preliminary.IsAccepted(),
					"All non-layout requirements pass the preliminary evaluation");
				snapshot.m_GlobalDescriptorSetLayoutProbed = true;
				snapshot.m_ProfileCapabilities.m_GlobalDescriptorSetLayoutSupported = false;
				EvaluateVulkanAdapterProfile(snapshot);
				context.Check(!snapshot.m_ProfileEvaluation.IsAccepted() &&
					snapshot.m_ProfileEvaluation.HasReason(
						VulkanDeviceProfileRejectionReason::GlobalDescriptorSetLayoutUnsupported),
					"A real layout probe result of unsupported rejects the adapter");
			}
			// All requirements pass and the layout probe reports supported.
			{
				VulkanAdapterCapabilitySnapshot snapshot =
					MakeAdapterSnapshot(0, "LayoutSupported",
						VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied);
				snapshot.m_GlobalDescriptorSetLayoutProbed = true;
				snapshot.m_ProfileCapabilities.m_GlobalDescriptorSetLayoutSupported = true;
				EvaluateVulkanAdapterProfile(snapshot);
				context.Check(snapshot.m_ProfileEvaluation.IsAccepted(),
					"A supported layout probe result accepts the adapter");
			}
			// Adapter indexing regression: a rejected adapter before an
			// accepted one must not shift selection bookkeeping.
			{
				auto rejectedCapabilities = satisfied;
				rejectedCapabilities.m_DescriptorBindingUpdateUnusedWhilePending = false;
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(MakeAdapterSnapshot(0, "Rejected First",
					VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, rejectedCapabilities));
				snapshots.push_back(MakeAdapterSnapshot(1, "Accepted Second",
					VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied));
				const VulkanAdapterSelectionResult result = SelectVulkanAdapter(snapshots, {});
				context.Check(result.IsSelected() && result.m_SelectedIndex == 1,
					"Default selection skips a rejected adapter and picks the accepted one");
			}
			// An adapter whose layout probe failed (probed stays false, but the
			// preliminary evaluation passed) is never selectable.
			{
				VulkanAdapterCapabilitySnapshot unverified = MakeAdapterSnapshot(0,
					"ProbeFailed", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, satisfied);
				unverified.m_GlobalDescriptorSetLayoutProbed = false;
				unverified.m_LayoutProbeError = "vkCreateDevice failed";
				std::vector<VulkanAdapterCapabilitySnapshot> snapshots;
				snapshots.push_back(std::move(unverified));
				const VulkanAdapterSelectionResult defaultResult = SelectVulkanAdapter(snapshots, {});
				const VulkanAdapterSelectionResult indexResult = SelectVulkanAdapter(snapshots,
					{ .m_Kind = VulkanAdapterSelectionKind::Index, .m_Index = 0 });
				context.Check(
					defaultResult.m_Status == VulkanAdapterSelectionStatus::NoAcceptedAdapter &&
					indexResult.m_Status == VulkanAdapterSelectionStatus::RejectedAdapter,
					"An adapter with a failed layout probe is never selectable");
			}
		}
#endif

#if GGLAB_ENABLE_VULKAN
		void RunVulkanFrameContractTests(SelfTestContext& context) noexcept
		{
			// Frame-slot ring selection and frame pairing domains. The slot
			// sequence is a ring over frameSlotCount; image indices come from
			// acquire and are recorded, never derived from the slot.
			{
				VulkanFrameIndexModel model(2);
				context.Check(model.GetFrameSlotCount() == 2,
					"frame-slot count is fixed at construction");
				const std::array<uint32_t, 5> slotSequence = { 0, 1, 0, 1, 0 };
				const std::array<uint32_t, 5> imageSequence = { 0, 1, 2, 0, 1 };
				for (uint32_t i = 0; i < 5; ++i)
				{
					const uint32_t slot = model.NextFrameSlot();
					context.Check(slot == slotSequence[i],
						"frame-slot ring returns slots in order");
					model.CommitFrame(slot, imageSequence[i]);
				}
				const auto& pairs = model.GetFramePairs();
				context.Check(pairs.size() == 5,
					"frame pairs are recorded per commit");
				bool domainsSeparate = true;
				for (uint32_t i = 0; i < 5; ++i)
				{
					domainsSeparate &= pairs[i].first == slotSequence[i] &&
						pairs[i].second == imageSequence[i];
				}
				context.Check(domainsSeparate,
					"frame-slot and backbuffer domains never cross");
				model.ResetFramePairs();
				context.Check(model.GetFramePairs().empty() && model.NextFrameSlot() == 0,
					"reset clears pairs and restarts the ring");
			}

			// Per-image tracked layout: new images start Undefined, present
			// leaves Present, recreate resets everything to Undefined.
			{
				VulkanImageLayoutTracker tracker;
				tracker.Reset(3);
				context.Check(tracker.GetImageCount() == 3,
					"layout tracker sizes to the image count");
				context.Check(tracker.Get(0) == VulkanPresentImageLayout::Undefined &&
					tracker.Get(2) == VulkanPresentImageLayout::Undefined,
					"new swapchain images start Undefined");
				tracker.Set(1, VulkanPresentImageLayout::Present);
				context.Check(tracker.Get(1) == VulkanPresentImageLayout::Present,
					"successful present updates the tracked state");
				tracker.Reset(2);
				context.Check(tracker.GetImageCount() == 2 &&
					tracker.Get(1) == VulkanPresentImageLayout::Undefined,
					"recreate resets the tracked state to Undefined");
				context.Check(tracker.Get(9) == VulkanPresentImageLayout::Undefined,
					"out-of-range reads degrade to Undefined");
			}

			// Active-frame transaction: Begin must terminate in exactly one
			// End or Abort; illegal transitions are rejected.
			{
				VulkanFrameSlotStateMachine machine;
				machine.Reset(2);
				context.Check(!machine.TryEnd(0) && !machine.TryAbort(0),
					"End/Abort without Begin are rejected");
				context.Check(machine.TryBegin(0) && machine.IsActive(0),
					"Begin activates the slot");
				context.Check(!machine.TryBegin(0),
					"double Begin is rejected");
				context.Check(machine.TryEnd(0) && !machine.IsActive(0),
					"End deactivates the slot");
				context.Check(!machine.TryEnd(0) && !machine.TryAbort(0),
					"double End and End after End are rejected");
				context.Check(machine.TryBegin(0) && machine.TryAbort(0),
					"a slot is reusable after End and aborts after Begin");
				context.Check(!machine.TryAbort(0) && !machine.TryEnd(0),
					"double Abort and End after Abort are rejected");
				context.Check(machine.TryBegin(0) && machine.TryEnd(0) && machine.TryBegin(0),
					"reuse after a completed transaction");
				context.Check(machine.TryAbort(0),
					"abort after Begin");
				context.Check(machine.TryBegin(0),
					"a slot starts a fresh transaction after Abort");
				context.Check(!machine.TryBegin(9),
					"out-of-range slots are rejected");
			}

			// Acquire/present result classification.
			{
				context.Check(ClassifyVulkanAcquireResult(VK_SUCCESS) ==
					VulkanAcquireOutcome::Acquired,
					"acquire SUCCESS hands over an image");
				context.Check(ClassifyVulkanAcquireResult(VK_SUBOPTIMAL_KHR) ==
					VulkanAcquireOutcome::RecreatePending,
					"acquire SUBOPTIMAL hands over an image and schedules recreate");
				context.Check(ClassifyVulkanAcquireResult(VK_ERROR_OUT_OF_DATE_KHR) ==
					VulkanAcquireOutcome::OutOfDate,
					"acquire OUT_OF_DATE hands over no image; the caller recreates and retries");
				context.Check(ClassifyVulkanAcquireResult(VK_ERROR_DEVICE_LOST) ==
					VulkanAcquireOutcome::Fatal,
					"other acquire results are fatal");
				context.Check(ClassifyVulkanPresentResult(VK_SUCCESS) ==
					VulkanPresentOutcome::Presented,
					"present SUCCESS");
				context.Check(ClassifyVulkanPresentResult(VK_SUBOPTIMAL_KHR) ==
					VulkanPresentOutcome::RecreatePending,
					"present SUBOPTIMAL is non-fatal and schedules recreate");
				context.Check(ClassifyVulkanPresentResult(VK_ERROR_OUT_OF_DATE_KHR) ==
					VulkanPresentOutcome::RecreatePending,
					"present OUT_OF_DATE keeps the submission valid");
				context.Check(ClassifyVulkanPresentResult(VK_ERROR_SURFACE_LOST_KHR) ==
					VulkanPresentOutcome::Failed,
					"other present results are failures");
			}

			// The combined submit+present transaction drives the runtime:
			// a failed submit never presents, and a non-fatal present keeps
			// the frame completed.
			{
				context.Check(ClassifySubmitPresentTransaction(VK_SUCCESS, VK_SUCCESS) ==
					VulkanFrameTransactionOutcome::Completed,
					"submit+present SUCCESS completes the frame");
				context.Check(ClassifySubmitPresentTransaction(VK_SUCCESS, VK_SUBOPTIMAL_KHR) ==
					VulkanFrameTransactionOutcome::RecreatePending,
					"submit SUCCESS + present SUBOPTIMAL completes and schedules recreate");
				context.Check(ClassifySubmitPresentTransaction(VK_SUCCESS, VK_ERROR_OUT_OF_DATE_KHR) ==
					VulkanFrameTransactionOutcome::RecreatePending,
					"submit SUCCESS + present OUT_OF_DATE keeps the submission valid");
				context.Check(ClassifySubmitPresentTransaction(VK_SUCCESS, VK_ERROR_SURFACE_LOST_KHR) ==
					VulkanFrameTransactionOutcome::PresentFailed,
					"submit SUCCESS + fatal present fails the runtime");
				context.Check(ClassifySubmitPresentTransaction(VK_ERROR_DEVICE_LOST, VK_SUCCESS) ==
					VulkanFrameTransactionOutcome::SubmitFailed,
					"a failed submit never reaches present");
			}

			// The frame-slot reuse gate only ever moves to a successfully
			// submitted timeline value; a failed submit leaves the previous
			// gate untouched so no future frame waits on an unsignaled value.
			{
				context.Check(UpdateSlotReuseGate(5, VK_SUCCESS, 6) == 6,
					"successful submit advances the reuse gate");
				context.Check(UpdateSlotReuseGate(5, VK_ERROR_DEVICE_LOST, 6) == 5,
					"failed submit never advances the reuse gate");
				context.Check(UpdateSlotReuseGate(0, VK_ERROR_INITIALIZATION_FAILED, 1) == 0,
					"a never-submitted value can never become a wait target");
			}

			// Present-mode policy: VSync on always selects FIFO; VSync off
			// prefers MAILBOX, then IMMEDIATE, then FIFO. This is the pure
			// selection the swapchain creation path feeds with the surface's
			// actual mode list.
			{
				const std::vector<VkPresentModeKHR> full{
					VK_PRESENT_MODE_FIFO_KHR,
					VK_PRESENT_MODE_IMMEDIATE_KHR,
					VK_PRESENT_MODE_MAILBOX_KHR,
				};
				const std::vector<VkPresentModeKHR> noMailbox{
					VK_PRESENT_MODE_FIFO_KHR,
					VK_PRESENT_MODE_IMMEDIATE_KHR,
				};
				const std::vector<VkPresentModeKHR> fifoOnly{
					VK_PRESENT_MODE_FIFO_KHR,
				};
				context.Check(SelectVulkanPresentModeFromList(full, true) ==
					VK_PRESENT_MODE_FIFO_KHR,
					"vsync on always selects FIFO");
				context.Check(SelectVulkanPresentModeFromList(full, false) ==
					VK_PRESENT_MODE_MAILBOX_KHR,
					"vsync off prefers MAILBOX");
				context.Check(SelectVulkanPresentModeFromList(noMailbox, false) ==
					VK_PRESENT_MODE_IMMEDIATE_KHR,
					"vsync off falls back to IMMEDIATE");
				context.Check(SelectVulkanPresentModeFromList(fifoOnly, false) ==
					VK_PRESENT_MODE_FIFO_KHR,
					"vsync off falls back to FIFO");
			}

			// Runtime health: fatal is not device lost. A surface-lost
			// present or an out-of-memory result stops the runtime but the
			// VkDevice is still usable, so cleanup still quiesces; only
			// VK_ERROR_DEVICE_LOST marks the device lost.
			{
				context.Check(!IsVulkanDeviceLostError(VK_ERROR_SURFACE_LOST_KHR),
					"SURFACE_LOST is not device lost");
				context.Check(!IsVulkanDeviceLostError(VK_ERROR_OUT_OF_HOST_MEMORY),
					"OUT_OF_HOST_MEMORY is not device lost");
				context.Check(!IsVulkanDeviceLostError(VK_ERROR_OUT_OF_DEVICE_MEMORY),
					"OUT_OF_DEVICE_MEMORY is not device lost");
				context.Check(!IsVulkanDeviceLostError(VK_ERROR_INITIALIZATION_FAILED),
					"INITIALIZATION_FAILED is not device lost");
				context.Check(IsVulkanDeviceLostError(VK_ERROR_DEVICE_LOST),
					"DEVICE_LOST is device lost");

				const VulkanRuntimeHealthState surfaceLost =
					UpdateVulkanRuntimeHealth({}, VK_ERROR_SURFACE_LOST_KHR);
				context.Check(surfaceLost.m_Fatal && !surfaceLost.m_DeviceLost,
					"a present SURFACE_LOST marks fatal but not device lost");
				const VulkanRuntimeHealthState deviceLost =
					UpdateVulkanRuntimeHealth({}, VK_ERROR_DEVICE_LOST);
				context.Check(deviceLost.m_Fatal && deviceLost.m_DeviceLost,
					"DEVICE_LOST marks fatal and device lost");
				const VulkanRuntimeHealthState hostOom =
					UpdateVulkanRuntimeHealth({}, VK_ERROR_OUT_OF_HOST_MEMORY);
				context.Check(hostOom.m_Fatal && !hostOom.m_DeviceLost,
					"OUT_OF_HOST_MEMORY marks fatal but not device lost");
				const VulkanRuntimeHealthState escalated =
					UpdateVulkanRuntimeHealth(surfaceLost, VK_ERROR_DEVICE_LOST);
				context.Check(escalated.m_Fatal && escalated.m_DeviceLost,
					"a later DEVICE_LOST escalates fatal health to device lost");
				const VulkanRuntimeHealthState unchanged =
					UpdateVulkanRuntimeHealth(deviceLost, VK_SUCCESS);
				context.Check(unchanged.m_Fatal && unchanged.m_DeviceLost,
					"SUCCESS never changes runtime health");
			}

			// The frame-slot reuse gate wait decides whether BeginFrame may
			// continue to command-pool reset and acquire: any failed wait
			// stops the frame before those steps.
			{
				context.Check(ClassifyVulkanBeginGateResult(VK_SUCCESS) ==
					VulkanBeginGateOutcome::Ready,
					"timeline wait success continues BeginFrame");
				context.Check(ClassifyVulkanBeginGateResult(VK_ERROR_DEVICE_LOST) ==
					VulkanBeginGateOutcome::Fatal,
					"device-lost wait stops BeginFrame before reset/acquire");
				context.Check(ClassifyVulkanBeginGateResult(VK_ERROR_SURFACE_LOST_KHR) ==
					VulkanBeginGateOutcome::Fatal,
					"any failed wait stops BeginFrame before reset/acquire");
			}
		}
#endif

		void RunVulkanCliContractTests(SelfTestContext& context) noexcept
		{
			const auto parse = [](std::initializer_list<std::string_view> arguments)
				{
					const std::vector<std::string_view> args(arguments);
					return ParseApplicationLaunchOptions(args);
				};

			// --adapter requires an explicit --rhi vulkan.
			{
				const auto result = parse({ "--adapter", "0" });
				context.Check(!result.IsValid() && result.m_Error.find("--rhi vulkan") !=
					std::string::npos,
					"--adapter without --rhi vulkan is a parse error");
			}
			{
				const auto result = parse({ "--rhi", "dx12", "--adapter", "0" });
				context.Check(!result.IsValid() && result.m_Error.find("--rhi vulkan") !=
					std::string::npos,
					"--rhi dx12 with --adapter is a parse error");
			}
			{
				const auto result = parse({ "--rhi", "vulkan", "--adapter", "0" });
				context.Check(result.IsValid() &&
					result.m_Options.m_RhiBackend == RHIBackendType::Vulkan &&
					result.m_Options.m_AdapterSelector == "0",
					"--rhi vulkan with --adapter is valid");
			}
			// --list-adapters stands alone; combining it with --adapter fails.
			{
				const auto result = parse({ "--list-adapters" });
				context.Check(result.IsValid() && result.m_Options.m_ListAdapters,
					"--list-adapters is valid without --rhi vulkan");
			}
			{
				const auto result = parse({ "--list-adapters", "--adapter", "0" });
				context.Check(!result.IsValid() && result.m_Error.find("--list-adapters") !=
					std::string::npos,
					"--list-adapters with --adapter is a parse error");
			}
			{
				const auto result = parse({ "--rhi", "vulkan", "--list-adapters" });
				context.Check(result.IsValid() && result.m_Options.m_ListAdapters &&
					result.m_Options.m_RhiBackend == RHIBackendType::Vulkan,
					"--rhi vulkan with --list-adapters is valid");
			}
			// --list-adapters is Vulkan inspection; an explicit DX12 backend
			// conflicts with it.
			{
				const auto result = parse({ "--rhi", "dx12", "--list-adapters" });
				context.Check(!result.IsValid() && result.m_Error.find("--list-adapters") !=
					std::string::npos,
					"--rhi dx12 with --list-adapters is a parse error");
			}
		}
	}

	void RunVulkanFormatContractTests(SelfTestContext& context) noexcept
	{
		// Every RHI format has a valid Vulkan resource mapping. The aspect
		// mask matches the shared RHI metadata through the effective aspect
		// contract: depth-stencil formats (including typeless R32Typeless)
		// expose their depth/stencil aspects instead of the generic color
		// aspect of the typeless family.
		bool allFormatsMapped = true;
		bool allAspectsMatch = true;
		for (uint32_t raw = static_cast<uint32_t>(RHIFormat::Unknown) + 1;
			raw < static_cast<uint32_t>(RHIFormat::Count); ++raw)
		{
			const RHIFormat format = static_cast<RHIFormat>(raw);
			allFormatsMapped &= IsVulkanFormatSupported(format);
			const RHIFormatInfo& rhiInfo = GetRHIFormatInfo(format);
			const RHITextureAspect effectiveAspects = rhiInfo.m_DepthStencilAspects !=
				RHITextureAspect::None
				? rhiInfo.m_DepthStencilAspects
				: rhiInfo.m_Aspects;
			const VkImageAspectFlags expected = ToVulkanImageAspectFlags(effectiveAspects);
			allAspectsMatch &= GetVulkanFormatInfo(format).m_Aspects == expected;
		}
		context.Check(allFormatsMapped,
			"Every RHI format maps to a supported Vulkan resource format");
		context.Check(allAspectsMatch,
			"Every RHI format aspect mask matches the shared RHI format metadata");

		// Typeless depth family: R32Typeless views as D32Float (DSV) and
		// R32Float (sampled), never as a color format.
		context.Check(IsVulkanViewFormatCompatible(RHIFormat::R32Typeless, RHIFormat::D32Float) &&
			IsVulkanViewFormatCompatible(RHIFormat::R32Typeless, RHIFormat::R32Float) &&
			!IsVulkanViewFormatCompatible(RHIFormat::R32Typeless, RHIFormat::R8G8B8A8Unorm),
			"R32Typeless accepts only the depth view family");
		context.Check(ToVulkanViewFormat(RHIFormat::R32Typeless, RHIFormat::R32Float) ==
			VK_FORMAT_D32_SFLOAT &&
			ToVulkanViewFormat(RHIFormat::R32Typeless, RHIFormat::D32Float) ==
			VK_FORMAT_D32_SFLOAT,
			"R32Typeless sampled views stay on the D32 depth image format");

		// RGBA8 typeless family: UNORM/SRGB views only, with mutable-format
		// creation required.
		context.Check(IsVulkanViewFormatCompatible(
			RHIFormat::R8G8B8A8Typeless, RHIFormat::R8G8B8A8Unorm) &&
			IsVulkanViewFormatCompatible(
				RHIFormat::R8G8B8A8Typeless, RHIFormat::R8G8B8A8UnormSrgb) &&
			!IsVulkanViewFormatCompatible(RHIFormat::R8G8B8A8Typeless, RHIFormat::R32Float),
			"R8G8B8A8Typeless accepts only the UNORM/SRGB view family");
		context.Check(NeedsVulkanMutableFormat(RHIFormat::R8G8B8A8Typeless) &&
			!NeedsVulkanMutableFormat(RHIFormat::R32Typeless) &&
			!NeedsVulkanMutableFormat(RHIFormat::R16G16B16A16Float),
			"Only non-depth typeless families require mutable-format images");
		context.Check(ToVulkanViewFormat(
			RHIFormat::R8G8B8A8Typeless, RHIFormat::R8G8B8A8UnormSrgb) ==
			VK_FORMAT_R8G8B8A8_SRGB,
			"RGBA8 typeless SRGB views resolve to the native SRGB format");

		// Non-typeless formats only view as themselves.
		context.Check(IsVulkanViewFormatCompatible(
			RHIFormat::R16G16B16A16Float, RHIFormat::R16G16B16A16Float) &&
			!IsVulkanViewFormatCompatible(
				RHIFormat::R16G16B16A16Float, RHIFormat::R16G16B16A16Typeless) &&
			!IsVulkanViewFormatCompatible(RHIFormat::R16G16B16A16Float, RHIFormat::R8Unorm),
			"Non-typeless formats accept exactly their own view format");

		// Usage lowering: native image usage flags and required format
		// features stay exact; Present contributes no ordinary feature.
		context.Check(ToVulkanImageUsageFlags(
			RHITextureUsage::Sampled | RHITextureUsage::RenderTarget) ==
			(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
			"Texture usage maps to the exact native image usage flags");
		context.Check(ToVulkanFormatFeatureFlags(
			RHITextureUsage::UnorderedAccess | RHITextureUsage::CopyDest) ==
			(VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT),
			"Texture usage maps to the exact required format features");
		context.Check(ToVulkanFormatFeatureFlags(RHITextureUsage::Present) == 0,
			"Present contributes no ordinary format feature");

		// Depth formats carry only depth/stencil aspects, never color.
		context.Check((GetVulkanFormatInfo(RHIFormat::D32Float).m_Aspects &
			VK_IMAGE_ASPECT_COLOR_BIT) == 0 &&
			(GetVulkanFormatInfo(RHIFormat::D24UnormS8Uint).m_Aspects &
				(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) ==
			(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
			"Depth/stencil formats expose depth and stencil aspects only");

		// View dimension lowering covers every RHI dimension.
		context.Check(ToVulkanImageViewType(RHITextureViewDimension::Texture2D) ==
			VK_IMAGE_VIEW_TYPE_2D &&
			ToVulkanImageViewType(RHITextureViewDimension::TextureCube) ==
			VK_IMAGE_VIEW_TYPE_CUBE &&
			ToVulkanImageViewType(RHITextureViewDimension::TextureCubeArray) ==
			VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
			"Texture view dimensions map to the exact native view types");

		// Sampler address modes map to the frozen native contract;
		// mirror-once lowers to mirror-clamp-to-edge, which needs the
		// samplerMirrorClampToEdge feature at sampler creation.
		context.Check(
			ToVulkanSamplerAddressMode(RHITextureAddressMode::Wrap) ==
			VK_SAMPLER_ADDRESS_MODE_REPEAT &&
			ToVulkanSamplerAddressMode(RHITextureAddressMode::Mirror) ==
			VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT &&
			ToVulkanSamplerAddressMode(RHITextureAddressMode::Clamp) ==
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
			ToVulkanSamplerAddressMode(RHITextureAddressMode::Border) ==
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER &&
			ToVulkanSamplerAddressMode(RHITextureAddressMode::MirrorOnce) ==
			VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
			"Sampler address modes map to the exact native address modes");

		// Non-power-of-two sample counts are rejected by the public
		// validator, so no backend conversion can silently downgrade them.
		const RHITextureDesc invalidSample{
			.m_Format = RHIFormat::R8G8B8A8Unorm,
			.m_Usage = RHITextureUsage::Sampled,
			.m_Extent = { 4, 4, 1 },
			.m_SampleCount = 3,
		};
		context.Check(!ValidateRHITextureDesc(invalidSample).IsValid(),
			"Non-power-of-two sample counts are rejected by validation");
	}

	void RunVulkanPortabilityContractTests(SelfTestContext& context) noexcept
	{
		// View min LOD is rejected without the extension and accepted with
		// it; zero clamp is always accepted.
		RHIPortabilityCapabilities withoutMinLod{};
		withoutMinLod.m_ImageViewMinLod = false;
		RHIPortabilityCapabilities withMinLod{};
		withMinLod.m_ImageViewMinLod = true;

		RHITextureViewDesc minLodView{};
		minLodView.m_ResourceMinLODClamp = 2.0f;
		context.Check(!ValidateRHITextureViewPortability(minLodView, withoutMinLod).IsValid() &&
			ValidateRHITextureViewPortability(minLodView, withMinLod).IsValid() &&
			ValidateRHITextureViewPortability({}, withoutMinLod).IsValid(),
			"Image-view min LOD requires VK_EXT_image_view_min_lod");

		// Sampler border colors: the three fixed colors are always legal,
		// arbitrary colors need the custom-border-color feature.
		RHIPortabilityCapabilities withoutCustomBorder{};
		withoutCustomBorder.m_CustomBorderColor = false;
		RHIPortabilityCapabilities withCustomBorder{};
		withCustomBorder.m_CustomBorderColor = true;

		RHISamplerDesc transparentBlack{};
		transparentBlack.m_AddressU = RHITextureAddressMode::Border;
		transparentBlack.m_BorderColor[0] = 0.0f;
		transparentBlack.m_BorderColor[1] = 0.0f;
		transparentBlack.m_BorderColor[2] = 0.0f;
		transparentBlack.m_BorderColor[3] = 0.0f;
		context.Check(ValidateRHISamplerPortability(transparentBlack, withoutCustomBorder).IsValid(),
			"Transparent black border color is always supported");

		RHISamplerDesc arbitraryColor{};
		arbitraryColor.m_AddressU = RHITextureAddressMode::Border;
		arbitraryColor.m_BorderColor[0] = 0.25f;
		arbitraryColor.m_BorderColor[1] = 0.5f;
		arbitraryColor.m_BorderColor[2] = 0.75f;
		arbitraryColor.m_BorderColor[3] = 1.0f;
		context.Check(
			!ValidateRHISamplerPortability(arbitraryColor, withoutCustomBorder).IsValid() &&
			ValidateRHISamplerPortability(arbitraryColor, withCustomBorder).IsValid(),
			"Arbitrary sampler border colors require custom-border-color");

		// The device policy never claims support that lacks a native
		// lowering: custom border colors, image-view min LOD, instance
		// divisors and sample quality stay disabled even when the hardware
		// reports them, while adopted core features pass through.
		RHIPortabilityCapabilities available{};
		available.m_ImageViewMinLod = true;
		available.m_CustomBorderColor = true;
		available.m_VertexAttributeDivisor = true;
		available.m_FillModeNonSolid = true;
		available.m_DepthClamp = true;
		available.m_DepthBiasClamp = true;
		available.m_IndependentBlend = true;
		available.m_SampleQuality = true;
		const RHIPortabilityCapabilities enabled = ApplyVulkanPortabilityPolicy(available);
		context.Check(!enabled.m_ImageViewMinLod && !enabled.m_CustomBorderColor &&
			!enabled.m_VertexAttributeDivisor && !enabled.m_SampleQuality,
			"Unlowered capabilities are never enabled by the device policy");
		context.Check(enabled.m_FillModeNonSolid && enabled.m_DepthClamp &&
			enabled.m_DepthBiasClamp && enabled.m_IndependentBlend,
			"Adopted core capabilities pass through the device policy");
	}

	void RunVulkanGraphicsPipelineContractTests(SelfTestContext& context) noexcept
	{
		RHIGraphicsPipelineDesc desc{};
		desc.m_VertexInput.m_VertexBuffers[0] = {
			.m_InputSlot = 0,
			.m_StrideInBytes = 20,
		};
		desc.m_VertexInput.m_VertexBufferCount = 1;
		desc.m_VertexInput.m_Attributes[0] = {
			.m_Location = 0,
			.m_Format = RHIFormat::R32G32B32Float,
			.m_InputSlot = 0,
			.m_AlignedByteOffset = 0,
		};
		desc.m_VertexInput.m_Attributes[1] = {
			.m_Location = 1,
			.m_Format = RHIFormat::R32G32Float,
			.m_InputSlot = 0,
			.m_AlignedByteOffset = 12,
		};
		desc.m_VertexInput.m_AttributeCount = 2;
		desc.m_Rasterizer.m_CullMode = RHICullMode::Back;
		desc.m_Rasterizer.m_FrontCounterClockwise = false;
		desc.m_DepthStencil = {
			.m_DepthTestEnable = true,
			.m_DepthWriteEnable = true,
			.m_DepthCompareOp = RHICompareOp::Greater,
		};
		desc.m_RenderTargetFormats[0] = RHIFormat::R8G8B8A8Unorm;
		desc.m_RenderTargetCount = 1;
		desc.m_DepthStencilFormat = RHIFormat::D32Float;

		const VulkanGraphicsPipelinePlan plan = BuildVulkanGraphicsPipelinePlan(desc);
		context.Check(plan.IsValid() &&
			plan.m_Topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST &&
			plan.m_VertexBindingCount == 1 && plan.m_VertexAttributeCount == 2 &&
			plan.m_VertexBindings[0].binding == 0 && plan.m_VertexBindings[0].stride == 20 &&
			plan.m_VertexAttributes[0].location == 0 &&
			plan.m_VertexAttributes[0].format == VK_FORMAT_R32G32B32_SFLOAT &&
			plan.m_VertexAttributes[1].location == 1 &&
			plan.m_VertexAttributes[1].offset == 12 &&
			plan.m_FrontFace == VK_FRONT_FACE_CLOCKWISE &&
			plan.m_CullMode == VK_CULL_MODE_BACK_BIT &&
			plan.m_DepthCompareOp == VK_COMPARE_OP_GREATER &&
			plan.m_ColorFormats[0] == VK_FORMAT_R8G8B8A8_UNORM &&
			plan.m_DepthFormat == VK_FORMAT_D32_SFLOAT,
			"Graphics pipeline lowering preserves vertex, winding, attachment and depth contracts");
		context.Check(ToVulkanFrontFace(false) == VK_FRONT_FACE_CLOCKWISE &&
			ToVulkanFrontFace(true) == VK_FRONT_FACE_COUNTER_CLOCKWISE,
			"Front-face lowering applies the coordinate policy exactly once");
		RHIGraphicsPipelineDesc duplicateLocation = desc;
		duplicateLocation.m_VertexInput.m_Attributes[1].m_Location = 0;
		context.Check(BuildVulkanGraphicsPipelinePlan(duplicateLocation).m_Error ==
			VulkanGraphicsPipelineError::InvalidVertexBinding,
			"Graphics pipeline lowering rejects duplicate vertex locations");
		RHIGraphicsPipelineDesc outsideStride = desc;
		outsideStride.m_VertexInput.m_Attributes[1].m_AlignedByteOffset = 16;
		context.Check(BuildVulkanGraphicsPipelinePlan(outsideStride).m_Error ==
			VulkanGraphicsPipelineError::InvalidVertexFormat,
			"Graphics pipeline lowering rejects attributes that exceed their binding stride");
		RHIGraphicsPipelineDesc mismatchedTopology = desc;
		mismatchedTopology.m_TopologyType = RHIPrimitiveTopologyType::Line;
		context.Check(BuildVulkanGraphicsPipelinePlan(mismatchedTopology).m_Error ==
			VulkanGraphicsPipelineError::InvalidTopology,
			"Graphics pipeline lowering rejects mismatched topology families");
		RHIGraphicsPipelineDesc invalidStencil = desc;
		invalidStencil.m_DepthStencil.m_StencilEnable = true;
		context.Check(BuildVulkanGraphicsPipelinePlan(invalidStencil).m_Error ==
			VulkanGraphicsPipelineError::InvalidDepthStencilFormat,
			"Graphics pipeline lowering rejects stencil state without a stencil format");
		RHIGraphicsPipelineDesc invalidSamples = desc;
		invalidSamples.m_SampleCount = 3;
		context.Check(BuildVulkanGraphicsPipelinePlan(invalidSamples).m_Error ==
			VulkanGraphicsPipelineError::InvalidSampleCount,
			"Graphics pipeline lowering rejects non-power-of-two sample counts");
	}

	void RunVulkanDescriptorArenaTests(SelfTestContext& context) noexcept
	{
		// Arena capacity matches the frozen backend-neutral contract.
		VulkanDescriptorIndexArena resourceArena(
			GGLabDescriptorCapacityContract.m_ResourceDescriptorCount);
		VulkanDescriptorIndexArena samplerArena(
			GGLabDescriptorCapacityContract.m_SamplerDescriptorCount);
		context.Check(resourceArena.GetCapacity() == 65'536 &&
			samplerArena.GetCapacity() == 2'048,
			"Descriptor arenas match the frozen capacity contract");

		// Index 0 is a legal descriptor index; exhaustion is reported
		// through std::nullopt, never by occupying a valid index.
		const std::optional<uint32_t> first = resourceArena.Allocate();
		const std::optional<uint32_t> second = resourceArena.Allocate();
		context.Check(first.has_value() && second.has_value() && *first != *second &&
			resourceArena.GetLiveCount() == 2,
			"Descriptor arena allocates distinct valid indices");

		resourceArena.Release(*first);
		context.Check(resourceArena.GetLiveCount() == 1,
			"Descriptor arena releases indices back to the free list");
		const std::optional<uint32_t> reused = resourceArena.Allocate();
		context.Check(reused.has_value() && *reused == *first &&
			resourceArena.GetLiveCount() == 2,
			"Descriptor arena reuses the most recently released index");

		// A full arena reports std::nullopt and keeps its count.
		VulkanDescriptorIndexArena tinyArena(2);
		const std::optional<uint32_t> a = tinyArena.Allocate();
		const std::optional<uint32_t> b = tinyArena.Allocate();
		context.Check(a.has_value() && b.has_value() && *a == 0 && *b == 1 &&
			!tinyArena.Allocate().has_value() && tinyArena.GetLiveCount() == 2,
			"Descriptor arena hands out [0, capacity) and reports exhaustion without "
			"corrupting its state");

		tinyArena.Release(*a);
		tinyArena.Release(*a);
		tinyArena.Release(tinyArena.GetCapacity());
		const std::optional<uint32_t> recycled = tinyArena.Allocate();
		context.Check(recycled.has_value() && *recycled == *a &&
			!tinyArena.Allocate().has_value() && tinyArena.GetLiveCount() == 2,
			"Descriptor arena ignores duplicate/out-of-range release without duplicating indices");

		tinyArena.Release(*b);
		tinyArena.Release(*recycled);
		context.Check(tinyArena.GetLiveCount() == 0,
			"Descriptor arena live count returns to zero after full release");
	}

	void RunVulkanDescriptorPublicationTests(SelfTestContext& context) noexcept
	{
		VulkanDescriptorPublicationArena arena(2);
		const std::optional<uint32_t> first = arena.Allocate();
		context.Check(first && *first == 0 &&
			arena.GetState(*first) ==
			VulkanDescriptorPublicationState::AllocatedUnpublished,
			"Descriptor publication starts in the private allocated state");
		context.Check(!arena.Publish(*first, 1),
			"Descriptor publication rejects a slot before its native write is ready");

		auto backing = std::make_shared<uint32_t>(17);
		std::weak_ptr<uint32_t> retainedBacking = backing;
		context.Check(arena.MarkDescriptorReady(*first, backing) && arena.Publish(*first, 1) &&
			arena.GetState(*first) == VulkanDescriptorPublicationState::Live,
			"Descriptor publication follows allocated, ready, and live states");
		backing.reset();
		context.Check(!retainedBacking.expired() &&
			!arena.MarkDescriptorReady(*first, std::make_shared<uint32_t>(18)),
			"A live descriptor retains its backing and rejects in-place overwrite");

		const RHIFenceHandle graphicsFence(4, 1);
		const RHIFencePoint lastPossibleUse(graphicsFence, 9);
		context.Check(arena.Retire(*first, { lastPossibleUse }) &&
			arena.GetState(*first) == VulkanDescriptorPublicationState::Retired,
			"A live descriptor enters retirement with its last possible graphics use");
		const RHIFencePoint laterOwnerUse(graphicsFence, 12);
		context.Check(arena.JoinRetirement(*first, {}, { &laterOwnerUse, 1 }),
			"A retired descriptor joins a later parent-resource retirement gate");
		arena.ReleaseCompleted([](const RHIFencePoint& point) noexcept
			{
				return point.m_Value <= 9;
			});
		context.Check(!retainedBacking.expired() &&
			arena.GetDiagnostics().m_RetiredCount == 1,
			"A later joined gate keeps the descriptor backing alive");
		arena.ReleaseCompleted([](const RHIFencePoint&) noexcept { return true; });
		context.Check(retainedBacking.expired() &&
			arena.GetState(*first) == VulkanDescriptorPublicationState::Free,
			"Completed retirement releases the descriptor backing and index together");

		const std::optional<uint32_t> reused = arena.Allocate();
		auto unpublishedBacking = std::make_shared<uint32_t>(21);
		std::weak_ptr<uint32_t> unpublishedWeak = unpublishedBacking;
		const bool ready = arena.MarkDescriptorReady(*reused, unpublishedBacking);
		unpublishedBacking.reset();
		context.Check(ready && arena.CancelUnpublished(*reused) && unpublishedWeak.expired(),
			"Unpublished descriptor cancellation releases private backing immediately");
		context.Check(arena.GetDiagnostics().m_InvalidTransitionCount == 2,
			"Descriptor publication diagnostics count invalid transitions");
	}

	void RunVulkanDescriptorGenerationTests(SelfTestContext& context) noexcept
	{
		VulkanDescriptorPublicationTracker tracker(2);
		const auto completed = [](const RHIFencePoint&) noexcept { return true; };
		context.Check(tracker.BeginFrameSnapshot(0, completed) &&
			tracker.BeginFrameSnapshot(1, completed),
			"Frame slots capture the current descriptor publication generation");
		const uint64_t replacementGeneration = tracker.PublishReplacement();
		RHIDescriptorRetirement retirement{};
		context.Check(replacementGeneration == 2 &&
			!tracker.TryDeriveRetirement(1, retirement),
			"An unsubmitted old-generation snapshot prevents descriptor retirement");

		const RHIFenceHandle graphicsFence(5, 1);
		context.Check(tracker.SubmitFrameSnapshot(0, { graphicsFence, 11 }) &&
			!tracker.TryDeriveRetirement(1, retirement),
			"Every old-generation snapshot must become submitted or be abandoned");
		context.Check(tracker.AbortFrameSnapshot(1) &&
			tracker.TryDeriveRetirement(1, retirement) &&
			retirement.m_LastPossibleGraphicsUse == RHIFencePoint(graphicsFence, 11),
			"Old-generation retirement derives the last submitted graphics fence");

		context.Check(tracker.BeginFrameSnapshot(1, completed) &&
			tracker.SubmitFrameSnapshot(1, { graphicsFence, 12 }) &&
			tracker.TryDeriveRetirement(1, retirement) && retirement.m_LastPossibleGraphicsUse.m_Value == 11,
			"New-generation submissions do not extend an old descriptor retirement gate");
		context.Check(!tracker.BeginFrameSnapshot(0,
			[](const RHIFencePoint&) noexcept { return false; }) &&
			tracker.BeginFrameSnapshot(0, completed),
			"A frame slot cannot replace its snapshot before the prior submission completes");

		VulkanDescriptorPublicationArena pendingArena(1);
		VulkanDescriptorPublicationTracker pendingTracker(1);
		const std::optional<uint32_t> pendingIndex = pendingArena.Allocate();
		auto pendingBacking = std::make_shared<uint32_t>(23);
		std::weak_ptr<uint32_t> pendingWeak = pendingBacking;
		const bool pendingPublished = pendingIndex &&
			pendingArena.MarkDescriptorReady(*pendingIndex, pendingBacking) &&
			pendingArena.Publish(*pendingIndex, pendingTracker.GetCurrentGeneration());
		pendingBacking.reset();
		const uint64_t lastReachableGeneration = pendingTracker.GetCurrentGeneration();
		context.Check(pendingPublished && pendingTracker.BeginFrameSnapshot(0, completed) &&
			pendingTracker.PublishReplacement() == 2 &&
			pendingArena.RequestRetirement(*pendingIndex, lastReachableGeneration, {}) &&
			pendingArena.GetState(*pendingIndex) == VulkanDescriptorPublicationState::Live &&
			pendingArena.GetDiagnostics().m_RetirementRequestedCount == 1 &&
			!pendingTracker.TryDeriveRetirement(lastReachableGeneration, retirement) &&
			!pendingWeak.expired(),
			"A retirement request stays live while an old descriptor snapshot is unsubmitted");
		context.Check(pendingTracker.SubmitFrameSnapshot(0, { graphicsFence, 15 }) &&
			pendingTracker.TryDeriveRetirement(lastReachableGeneration, retirement) &&
			pendingArena.CompleteRetirementRequest(*pendingIndex, retirement) &&
			pendingArena.GetState(*pendingIndex) == VulkanDescriptorPublicationState::Retired &&
			retirement.m_LastPossibleGraphicsUse == RHIFencePoint(graphicsFence, 15),
			"A submitted snapshot supplies the descriptor retirement fence");

		VulkanDescriptorPublicationArena abandonedArena(1);
		VulkanDescriptorPublicationTracker abandonedTracker(1);
		const std::optional<uint32_t> abandonedIndex = abandonedArena.Allocate();
		auto abandonedBacking = std::make_shared<uint32_t>(29);
		const bool abandonedPublished = abandonedIndex &&
			abandonedArena.MarkDescriptorReady(*abandonedIndex, abandonedBacking) &&
			abandonedArena.Publish(*abandonedIndex, abandonedTracker.GetCurrentGeneration());
		const uint64_t abandonedGeneration = abandonedTracker.GetCurrentGeneration();
		context.Check(abandonedPublished && abandonedTracker.BeginFrameSnapshot(0, completed) &&
			abandonedTracker.PublishReplacement() == 2 &&
			abandonedArena.RequestRetirement(*abandonedIndex, abandonedGeneration, {}) &&
			abandonedTracker.AbortFrameSnapshot(0) &&
			abandonedTracker.TryDeriveRetirement(abandonedGeneration, retirement) &&
			!retirement.m_LastPossibleGraphicsUse.IsValid() &&
			abandonedArena.CompleteRetirementRequest(*abandonedIndex, retirement),
			"A descriptor-unused abort abandons the snapshot without inventing a fence gate");
	}

	void RunVulkanBindingLayoutTests(SelfTestContext& context) noexcept
	{
		RHIBindingLayoutDesc desc{};
		desc.m_Slots[desc.m_SlotCount++] = {
			.m_Type = RHIBindingType::PushConstants,
			.m_Visibility = RHIShaderStage::All,
			.m_Binding = 2,
			.m_SizeInBytes = 16,
		};
		desc.m_Slots[desc.m_SlotCount++] = {
			.m_Type = RHIBindingType::SampledTexture,
			.m_Visibility = RHIShaderStage::Pixel,
			.m_Binding = 0,
		};
		desc.m_Slots[desc.m_SlotCount++] = {
			.m_Type = RHIBindingType::PushConstants,
			.m_Visibility = RHIShaderStage::All,
			.m_Binding = 1,
			.m_SizeInBytes = 16,
		};
		desc.m_Slots[desc.m_SlotCount++] = {
			.m_Type = RHIBindingType::BindlessResourceTable,
			.m_Visibility = RHIShaderStage::All,
			.m_Count = 0,
		};

		const VulkanBindingLayoutPlan plan = BuildVulkanBindingLayoutPlan(desc, 64 * 1024);
		context.Check(plan.IsValid() && plan.m_Set0BindingCount == 3 &&
			plan.m_DynamicOffsetCount == 2,
			"Vulkan binding layout separates fixed set 0 from the global descriptor set");
		context.Check(plan.m_Set0Bindings[0].m_Binding == 1 &&
			plan.m_Set0Bindings[1].m_Binding == 2 &&
			plan.m_Set0Bindings[2].m_Binding == 32,
			"Set-0 bindings use the shared register-class shifts and native order");
		context.Check(plan.GetDynamicOffsetSlot(2) == 0 &&
			plan.GetDynamicOffsetSlot(0) == 1,
			"Dynamic offsets follow native binding order instead of logical call order");

		VulkanDynamicUniformState state;
		const std::array<uint32_t, 2> firstUpdate{ 3, 7 };
		const std::array<uint32_t, 1> secondUpdate{ 11 };
		const bool initialized = state.Initialize(plan);
		const bool firstApplied = state.UpdateShadow(0, firstUpdate, 1);
		const bool secondApplied = state.UpdateShadow(0, secondUpdate, 3);
		std::array<uint32_t, 4> shadowWords{};
		const std::span<const std::byte> shadow = state.GetShadow(0);
		if (shadow.size() == sizeof(shadowWords))
		{
			std::memcpy(shadowWords.data(), shadow.data(), shadow.size());
		}
		context.Check(initialized && firstApplied && secondApplied &&
			shadowWords == std::array<uint32_t, 4>{ 0, 3, 7, 11 },
			"Push-constant destOffset uses 32-bit words and partial updates preserve shadow data");
		context.Check(!state.UpdateShadow(0, firstUpdate, 3),
			"Push-constant shadow updates reject ranges beyond the logical slot");

		RHIBindingLayoutDesc oversized = desc;
		oversized.m_Slots[0].m_SizeInBytes = 128;
		context.Check(BuildVulkanBindingLayoutPlan(oversized, 64).m_Error ==
			VulkanBindingLayoutError::InvalidPushConstantSize,
			"Vulkan binding layout rejects dynamic uniform ranges beyond the device limit");
		RHIBindingLayoutDesc duplicate{};
		duplicate.m_Slots[duplicate.m_SlotCount++] = desc.m_Slots[0];
		duplicate.m_Slots[duplicate.m_SlotCount++] = desc.m_Slots[0];
		context.Check(BuildVulkanBindingLayoutPlan(duplicate, 64 * 1024).m_Error ==
			VulkanBindingLayoutError::DuplicateNativeBinding,
			"Vulkan binding layout rejects duplicate native set-0 bindings");
	}

	void RunVulkanDynamicUniformArenaTests(SelfTestContext& context) noexcept
	{
		VulkanDynamicUniformArena arena({
			.m_PageSizeInBytes = 64,
			.m_MaxPageCount = 2,
			.m_Alignment = 16,
			});
		const VulkanDynamicUniformArenaAllocation first = arena.Allocate(12);
		const VulkanDynamicUniformArenaAllocation second = arena.Allocate(48);
		const VulkanDynamicUniformArenaAllocation third = arena.Allocate(16);
		const VulkanDynamicUniformArenaAllocation overflow = arena.Allocate(64);
		context.Check(first.m_OffsetInBytes == 0 && second.m_OffsetInBytes == 16 &&
			third.m_OffsetInBytes == 64 && third.m_PageIndex == 1,
			"Dynamic uniform allocation honors alignment and advances to the next page");
		context.Check(!overflow.IsValid() && arena.GetDiagnostics().m_OverflowCount == 1 &&
			arena.GetDiagnostics().m_PageCount == 2,
			"Dynamic uniform allocation reports hard-cap overflow without wrapping");
		const uint32_t highWater = arena.GetDiagnostics().m_HighWaterMarkInBytes;
		arena.Reset();
		const VulkanDynamicUniformArenaAllocation reused = arena.Allocate(16);
		context.Check(reused.m_OffsetInBytes == 0 && reused.m_PageIndex == 0 &&
			arena.GetDiagnostics().m_HighWaterMarkInBytes == highWater,
			"Dynamic uniform pages reset for frame-slot reuse while preserving high-water diagnostics");

		VulkanDynamicUniformArena normalizedPages({
			.m_PageSizeInBytes = 48,
			.m_MaxPageCount = 2,
			.m_Alignment = 32,
			});
		const VulkanDynamicUniformArenaAllocation normalizedFirst = normalizedPages.Allocate(48);
		const VulkanDynamicUniformArenaAllocation normalizedSecond = normalizedPages.Allocate(16);
		context.Check(normalizedFirst.m_OffsetInBytes == 0 &&
			normalizedSecond.m_OffsetInBytes == 64 && normalizedSecond.m_PageIndex == 1,
			"Dynamic uniform page starts remain aligned when page size is not an alignment multiple");

		VulkanDynamicUniformArena overflowingConfig({
			.m_PageSizeInBytes = UINT32_MAX,
			.m_MaxPageCount = 2,
			.m_Alignment = UINT32_MAX,
			});
		context.Check(overflowingConfig.GetCapacityInBytes() == 0 &&
			!overflowingConfig.Allocate(16).IsValid() &&
			overflowingConfig.GetDiagnostics().m_OverflowCount == 1,
			"Dynamic uniform configuration rejects address-space overflow without wrapping");
	}

	void RunVulkanViewNormalizationTests(SelfTestContext& context) noexcept
	{
		const RHITextureDesc depthResource{
			.m_Format = RHIFormat::R32Typeless,
			.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::DepthStencil,
			.m_Extent = { 1280, 720, 1 },
			.m_ArraySize = 1,
			.m_MipLevels = 4,
		};

		// Unknown format and All aspects resolve to the resource contract;
		// the sampled R32Float view stays on the depth aspect with the
		// native D32 format.
		RHITextureViewDesc sampledView{};
		sampledView.m_Type = RHITextureViewType::ShaderResource;
		sampledView.m_Dimension = RHITextureViewDimension::Texture2D;
		sampledView.m_Format = RHIFormat::R32Float;
		sampledView.m_Subresources = { .m_MipCount = 1, .m_ArraySliceCount = 1 };
		const auto normalized = NormalizeVulkanTextureView(depthResource, sampledView);
		context.Check(normalized.has_value() &&
			normalized->m_EffectiveFormat == RHIFormat::R32Float &&
			normalized->m_NativeFormat == VK_FORMAT_D32_SFLOAT &&
			normalized->m_AspectMask == VK_IMAGE_ASPECT_DEPTH_BIT,
			"R32Typeless sampled views normalize to the depth aspect on D32");

		// Remaining ranges expand to the resource extents.
		context.Check(normalized.has_value() &&
			normalized->m_Range.m_MipCount == 1 &&
			normalized->m_Range.m_ArraySliceCount == 1,
			"Explicit ranges are preserved by normalization");
		RHITextureViewDesc remainingView = sampledView;
		remainingView.m_Subresources = {};
		const auto remaining = NormalizeVulkanTextureView(depthResource, remainingView);
		context.Check(remaining.has_value() &&
			remaining->m_Range.m_MipCount == depthResource.m_MipLevels &&
			remaining->m_Range.m_ArraySliceCount == depthResource.m_ArraySize &&
			remaining->m_AspectMask == VK_IMAGE_ASPECT_DEPTH_BIT,
			"Remaining ranges expand to the resource extents");

		// Defaulted view format resolves to the resource format.
		RHITextureViewDesc defaultedView{};
		defaultedView.m_Dimension = RHITextureViewDimension::Texture2D;
		const auto defaulted = NormalizeVulkanTextureView(
			RHITextureDesc{ .m_Format = RHIFormat::R16G16B16A16Float,
				.m_Usage = RHITextureUsage::Sampled, .m_Extent = { 4, 4, 1 } },
			defaultedView);
		context.Check(defaulted.has_value() &&
			defaulted->m_EffectiveFormat == RHIFormat::R16G16B16A16Float &&
			defaulted->m_NativeFormat == VK_FORMAT_R16G16B16A16_SFLOAT &&
			defaulted->m_AspectMask == VK_IMAGE_ASPECT_COLOR_BIT,
			"Defaulted view format and aspects resolve to the resource contract");

		// Explicit illegal aspects are rejected, never silently replaced.
		RHITextureViewDesc colorAspectOnDepth = sampledView;
		colorAspectOnDepth.m_Subresources.m_Aspects = RHITextureAspect::Color;
		context.Check(!NormalizeVulkanTextureView(depthResource, colorAspectOnDepth).has_value(),
			"Explicit color aspect on a depth resource is rejected");

		// Out-of-range base subresources are rejected.
		RHITextureViewDesc baseOutOfRange = sampledView;
		baseOutOfRange.m_Subresources = { .m_BaseMip = 4, .m_MipCount = 1 };
		context.Check(!NormalizeVulkanTextureView(depthResource, baseOutOfRange).has_value(),
			"Out-of-range base mip is rejected");

		// Unknown dimension (the RHI default) derives from the resource
		// dimension and array size; the public validator accepts it.
		const RHITextureDesc derivedResource{
			.m_Format = RHIFormat::R8G8B8A8Unorm,
			.m_Usage = RHITextureUsage::Sampled,
			.m_Extent = { 4, 4, 1 },
			.m_ArraySize = 4,
		};
		RHITextureViewDesc derivedView{};
		derivedView.m_Dimension = RHITextureViewDimension::Unknown;
		const auto derived = NormalizeVulkanTextureView(derivedResource, derivedView);
		context.Check(derived.has_value() &&
			derived->m_ViewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY &&
			derived->m_EffectiveDimension == RHITextureViewDimension::Texture2DArray,
			"Unknown view dimension derives from the resource array size");

		// Unknown and the equivalent explicit dimension collapse to the
		// same canonical cache identity.
		RHITextureViewDesc explicitView = derivedView;
		explicitView.m_Dimension = RHITextureViewDimension::Texture2DArray;
		const auto explicitNormalized = NormalizeVulkanTextureView(derivedResource, explicitView);
		context.Check(explicitNormalized.has_value() &&
			explicitNormalized->m_EffectiveDimension ==
			derived->m_EffectiveDimension,
			"Explicit and derived dimensions normalize to the same effective dimension");
		RHITextureViewDesc canonicalUnknown = derivedView;
		canonicalUnknown.m_Format = derived->m_EffectiveFormat;
		canonicalUnknown.m_Dimension = derived->m_EffectiveDimension;
		canonicalUnknown.m_Subresources = derived->m_Range;
		RHITextureViewDesc canonicalExplicit = explicitView;
		canonicalExplicit.m_Format = explicitNormalized->m_EffectiveFormat;
		canonicalExplicit.m_Dimension = explicitNormalized->m_EffectiveDimension;
		canonicalExplicit.m_Subresources = explicitNormalized->m_Range;
		context.Check(RHITextureViewKey{ RHITextureHandle{ 3, 1 }, canonicalUnknown } ==
			RHITextureViewKey{ RHITextureHandle{ 3, 1 }, canonicalExplicit },
			"Unknown and explicit dimensions share one canonical cache key");
	}

	void RunVulkanContractSelfTests(SelfTestContext& context) noexcept
	{
		RunDescriptorCapacityTests(context);
		RunShaderBindingABITests(context);
		RunDeviceProfileTests(context);
		RunCoordinatePolicyTests(context);
		RunVulkanBarrierContractTests(context);
		RunVulkanTextureCopyContractTests(context);
		RunShaderArtifactContractTests(context);
		RunVulkanCliContractTests(context);
		RunVulkanFormatContractTests(context);
		RunVulkanPortabilityContractTests(context);
		RunVulkanGraphicsPipelineContractTests(context);
		RunVulkanDescriptorArenaTests(context);
		RunVulkanDescriptorPublicationTests(context);
		RunVulkanDescriptorGenerationTests(context);
		RunVulkanBindingLayoutTests(context);
		RunVulkanDynamicUniformArenaTests(context);
		RunVulkanViewNormalizationTests(context);
#if GGLAB_ENABLE_VULKAN
		RunVulkanBootstrapSelectionTests(context);
		RunVulkanFrameContractTests(context);
#endif
	}
}
