#include "Core/Precompiled.h"
#include "Application/SelfTest/VulkanContractSelfTests.h"
#include "Graphics/RHI/RHICoordinatePolicy.h"
#include "Graphics/RHI/RHIDescriptorCapacityContract.h"
#include "Graphics/RHI/Vulkan/VulkanCoordinatePolicy.h"
#include "Graphics/RHI/Vulkan/VulkanDeviceProfile.h"
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"

namespace gglab
{
	namespace
	{
		VulkanDeviceProfileCapabilities MakeSupportedCapabilities() noexcept
		{
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
				.m_ResourceDescriptorCapacity =
					GGLabDescriptorCapacityContract.m_ResourceDescriptorCount,
				.m_SamplerDescriptorCapacity =
					GGLabDescriptorCapacityContract.m_SamplerDescriptorCount,
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

			struct FixedBindingCase
			{
				VulkanShaderRegisterClass m_RegisterClass;
				uint32_t m_ExpectedBinding;
				std::string_view m_CheckName;
			};
			constexpr std::array FixedBindingCases{
				FixedBindingCase{ VulkanShaderRegisterClass::ConstantBuffer, 3,
					"Vulkan b-register shift is 0" },
				FixedBindingCase{ VulkanShaderRegisterClass::ShaderResource, 35,
					"Vulkan t-register shift is 32" },
				FixedBindingCase{ VulkanShaderRegisterClass::UnorderedAccess, 67,
					"Vulkan u-register shift is 64" },
				FixedBindingCase{ VulkanShaderRegisterClass::Sampler, 99,
					"Vulkan s-register shift is 96" },
			};
			for (const auto& testCase : FixedBindingCases)
			{
				const auto result = EvaluateVulkanFixedShaderBinding(testCase.m_RegisterClass, 3, 0);
				context.Check(result.IsSupported() && result.m_Location.m_DescriptorSet == 0 &&
					result.m_Location.m_Binding == testCase.m_ExpectedBinding, testCase.m_CheckName);
			}

			const auto reservedSpace = EvaluateVulkanFixedShaderBinding(
				VulkanShaderRegisterClass::ShaderResource, 0, 1);
			context.Check(!reservedSpace.IsSupported() && reservedSpace.m_RejectionReason ==
					VulkanShaderBindingRejectionReason::ReservedGlobalHeapRegisterSpace,
				"Fixed bindings reject HLSL space1 reserved for global heaps");
			const auto unsupportedSpace = EvaluateVulkanFixedShaderBinding(
				VulkanShaderRegisterClass::ShaderResource, 0, 2);
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
			context.Check(EvaluateVulkanV1DeviceProfile(supported).IsAccepted(),
				"Vulkan v1 profile accepts its exact minimum required capabilities");
			context.Check(!supported.m_DescriptorBindingVariableDescriptorCount &&
				!supported.m_DescriptorBindingUniformBufferUpdateAfterBind &&
				!supported.m_DescriptorBindingStorageBufferUpdateAfterBind &&
				!supported.m_ShaderUniformBufferArrayNonUniformIndexing &&
				!supported.m_ShaderStorageBufferArrayNonUniformIndexing &&
				EvaluateVulkanV1DeviceProfile(supported).IsAccepted(),
				"Vulkan v1 profile does not require variable-count or bindless-buffer features");

			auto multipleMissing = supported;
			multipleMissing.m_DynamicRendering = false;
			multipleMissing.m_SamplerAnisotropy = false;
			const auto multipleMissingEvaluation = EvaluateVulkanV1DeviceProfile(multipleMissing);
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
				const auto evaluation = EvaluateVulkanV1DeviceProfile(missing);
				context.Check(!evaluation.IsAccepted() && evaluation.m_RejectionReasonCount == 1 &&
					evaluation.HasReason(testCase.m_Reason), testCase.m_CheckName);
			}

			auto oldApi = supported;
			oldApi.m_ApiVersion = { 1, 2 };
			const auto oldApiEvaluation = EvaluateVulkanV1DeviceProfile(oldApi);
			context.Check(oldApiEvaluation.m_RejectionReasonCount == 1 && oldApiEvaluation.HasReason(
				VulkanDeviceProfileRejectionReason::ApiVersionTooLow),
				"Vulkan profile rejects API versions below 1.3 explicitly");

			auto insufficientResources = supported;
			--insufficientResources.m_ResourceDescriptorCapacity;
			const auto resourceEvaluation = EvaluateVulkanV1DeviceProfile(insufficientResources);
			context.Check(resourceEvaluation.m_RejectionReasonCount == 1 && resourceEvaluation.HasReason(
				VulkanDeviceProfileRejectionReason::ResourceDescriptorCapacityInsufficient),
				"Vulkan profile rejects resource descriptor capacity below 65,536");

			auto insufficientSamplers = supported;
			--insufficientSamplers.m_SamplerDescriptorCapacity;
			const auto samplerEvaluation = EvaluateVulkanV1DeviceProfile(insufficientSamplers);
			context.Check(samplerEvaluation.m_RejectionReasonCount == 1 && samplerEvaluation.HasReason(
				VulkanDeviceProfileRejectionReason::SamplerDescriptorCapacityInsufficient),
				"Vulkan profile rejects sampler descriptor capacity below 2,048");
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
	}

	void RunVulkanContractSelfTests(SelfTestContext& context) noexcept
	{
		RunDescriptorCapacityTests(context);
		RunShaderBindingABITests(context);
		RunDeviceProfileTests(context);
		RunCoordinatePolicyTests(context);
	}
}
