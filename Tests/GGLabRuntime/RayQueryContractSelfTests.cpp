#include "RayQueryContractSelfTests.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/RHI/RHIAccelerationStructure.h"
#include "Graphics/RHI/DX12/Utility/DX12BindingLayoutUtils.h"
#include "Graphics/RHI/DX12/DX12RayQuerySupport.h"
#include "Graphics/RHI/DX12/DX12PipelineSystem.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/AccelerationStructureStore.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"
#include "Graphics/RHI/Vulkan/VulkanRayQuerySupport.h"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <type_traits>

namespace gglab
{
	namespace
	{
		void RunDX12PipelineIdentityTests(SelfTestContext& context) noexcept
		{
			for (const bool compute : { false, true })
			{
				const DX12PipelineIdentity identity{
					.m_BindingLayout = { 0, 1 }, .m_IsCompute = compute };
				auto other = identity;
				other.m_BindingLayout = { 1, 1 };
				context.Check(identity != other,
					"Pipeline identity distinguishes logical layouts even with equal native identities");
				other.m_BindingLayout = { 0, 2 };
				context.Check(identity != other,
					"Pipeline identity retains the binding-layout generation");
				other = identity;
				other.m_IsCompute = !compute;
				context.Check(identity != other,
					"Pipeline identity distinguishes graphics and compute bindings");
			}
			context.Check(Test(RHIStage::All, RHIStage::AccelerationStructureBuild) &&
				ToD3D12BarrierSync(RHIStage::All) == D3D12_BARRIER_SYNC_ALL &&
				ToD3D12BarrierSync(RHIStage::AccelerationStructureBuild) ==
					D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE,
				"All includes AS builds while DX12 retains explicit and universal synchronization scopes");
		}

		constexpr RHIASBuildLimits Limits{
			.m_MaxPrimitiveCount = 1024,
			.m_MaxInstanceCount = 1024,
			.m_ScratchAlignmentInBytes = 256,
		};
		constexpr RHIBufferDesc GeometryBuffer{ .m_SizeInBytes = 4096 };

		[[nodiscard]] RHIASIndexedTriangles Triangle() noexcept
		{
			return {
				.m_Vertices = { RHIBufferHandle{ 0, 1 }, 64, 124 },
				.m_VertexCount = 3,
				.m_VertexStrideInBytes = 56,
				.m_Indices = { RHIBufferHandle{ 1, 1 }, 12, 12 },
				.m_IndexCount = 3,
			};
		}

		void RunCapabilityTests(SelfTestContext& context) noexcept
		{
			const RHIRayQueryCapabilities absent{};
			context.Check(!absent.m_Supported && !absent.m_Enabled &&
				absent.m_UnavailableReason == RHIRayQueryUnavailableReason::BackendUnavailable,
				"An unimplemented device does not claim ray-query support or enablement");
			const auto tier10 = EvaluateDX12RayQuerySupport(
				D3D12_RAYTRACING_TIER_1_0, D3D_SHADER_MODEL_6_6);
			context.Check(!tier10.m_Supported && !tier10.m_Enabled &&
				tier10.m_UnavailableReason == RHIRayQueryUnavailableReason::InlineQueryUnavailable,
				"DXR tier 1.0 is not sufficient for inline ray queries");
			const auto shader64 = EvaluateDX12RayQuerySupport(
				D3D12_RAYTRACING_TIER_1_1, D3D_SHADER_MODEL_6_4);
			context.Check(!shader64.m_Supported &&
				shader64.m_UnavailableReason == RHIRayQueryUnavailableReason::ShaderModelUnavailable,
				"DX12 reports the missing shader-model requirement independently of the RT tier");
			const auto dx12 = EvaluateDX12RayQuerySupport(
				D3D12_RAYTRACING_TIER_1_1, D3D_SHADER_MODEL_6_6);
			context.Check(dx12.m_Supported && !dx12.m_Enabled &&
				dx12.m_UnavailableReason == RHIRayQueryUnavailableReason::None &&
				dx12.m_BuildLimits.m_MaxPrimitiveCount != 0 &&
				dx12.m_BuildLimits.m_MaxInstanceCount != 0 &&
				dx12.m_BuildLimits.m_ScratchAlignmentInBytes == 256,
				"DX12 support exposes build limits without claiming that a device has been created");
#if GGLAB_ENABLE_VULKAN
			const VulkanRayQueryAvailability complete{
				.m_AccelerationStructureExtension = true,
				.m_RayQueryExtension = true,
				.m_DeferredHostOperationsExtension = true,
				.m_AccelerationStructure = true,
				.m_RayQuery = true,
				.m_BufferDeviceAddress = true,
				.m_BuildLimits = Limits,
				.m_MaxGeometryCount = 1,
				.m_MaxDescriptorSetAccelerationStructures = 1,
				.m_MaxPerStageDescriptorAccelerationStructures = 1,
			};
			const auto supported = EvaluateVulkanRayQuerySupport(complete);
			context.Check(supported.m_Supported && !supported.m_Enabled &&
				supported.m_BuildLimits.m_ScratchAlignmentInBytes == Limits.m_ScratchAlignmentInBytes,
				"A complete Vulkan set is supported; only successful device creation may enable it");
			struct MissingFeature
			{
				bool VulkanRayQueryAvailability::* m_Member;
				RHIRayQueryUnavailableReason m_Reason;
			};
			constexpr std::array cases
			{
				MissingFeature{ &VulkanRayQueryAvailability::m_AccelerationStructureExtension, RHIRayQueryUnavailableReason::RequiredExtensionsUnavailable },
				MissingFeature{ &VulkanRayQueryAvailability::m_RayQueryExtension, RHIRayQueryUnavailableReason::RequiredExtensionsUnavailable },
				MissingFeature{ &VulkanRayQueryAvailability::m_DeferredHostOperationsExtension, RHIRayQueryUnavailableReason::RequiredExtensionsUnavailable },
				MissingFeature{ &VulkanRayQueryAvailability::m_AccelerationStructure, RHIRayQueryUnavailableReason::AccelerationStructureUnavailable },
				MissingFeature{ &VulkanRayQueryAvailability::m_RayQuery, RHIRayQueryUnavailableReason::InlineQueryUnavailable },
				MissingFeature{ &VulkanRayQueryAvailability::m_BufferDeviceAddress, RHIRayQueryUnavailableReason::DeviceAddressUnavailable },
			};
			for (const auto& missing : cases)
			{
				auto available = complete;
				available.*missing.m_Member = false;
				const auto result = EvaluateVulkanRayQuerySupport(available);
				context.Check(!result.m_Supported && !result.m_Enabled &&
					result.m_UnavailableReason == missing.m_Reason &&
					result.m_BuildLimits.m_MaxPrimitiveCount == 0,
					"Partial Vulkan feature sets report their missing prerequisite without partial enablement");
			}
			for (uint32_t field = 0; field < 6; ++field)
			{
				auto available = complete;
				switch (field)
				{
				case 0:
					available.m_BuildLimits.m_MaxPrimitiveCount = 0;
					break;
				case 1:
					available.m_BuildLimits.m_MaxInstanceCount = 0;
					break;
				case 2:
					available.m_BuildLimits.m_ScratchAlignmentInBytes = 3;
					break;
				case 3:
					available.m_MaxGeometryCount = 0;
					break;
				case 4:
					available.m_MaxDescriptorSetAccelerationStructures = 0;
					break;
				default:
					available.m_MaxPerStageDescriptorAccelerationStructures = 0;
					break;
				}
				const auto result = EvaluateVulkanRayQuerySupport(available);
				context.Check(!result.m_Supported && !result.m_Enabled &&
					result.m_UnavailableReason == RHIRayQueryUnavailableReason::BuildLimitsUnavailable,
					"Missing build or fixed-descriptor limits cannot produce a supported Vulkan RT set");
			}
#endif
		}

		void RunTriangleTests(SelfTestContext& context) noexcept
		{
			const auto validate = [](const RHIASIndexedTriangles& input) noexcept
				{
					return ValidateRHIASIndexedTriangles(input, GeometryBuffer, GeometryBuffer, Limits);
				};
			auto input = Triangle();
			context.Check(validate(input).IsValid(),
				"Interleaved 56-byte vertices need only the last position, not a trailing vertex stride");
			input.m_Vertices.m_SizeInBytes = 123;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidBufferRange,
				"A position truncated by one byte rejects the BLAS input");
			input = Triangle();
			input.m_Vertices.m_OffsetInBytes = std::numeric_limits<uint64_t>::max() - 8;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidBufferRange,
				"Buffer range addition cannot wrap around to authorize an out-of-bounds position");
			input = Triangle();
			input.m_Vertices.m_Buffer = { 0, 0 };
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidBuffer,
				"An index with an invalid handle generation is not a build input");
			input = Triangle();
			input.m_VertexCount = 0;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidVertexLayout,
				"An empty position stream cannot underflow the last-vertex calculation");
			input = Triangle();
			input.m_VertexStrideInBytes = 54;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidVertexLayout,
				"Position strides must preserve float alignment");
			input = Triangle();
			input.m_VertexFormat = RHIFormat::R32G32Float;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidVertexFormat,
				"The initial triangle contract requires three float position components");
			input = Triangle();
			input.m_IndexFormat = RHIFormat::R32Float;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidIndexFormat,
				"The source pool's R32 unsigned indices cannot be relabeled as floats");
			input = Triangle();
			input.m_IndexCount = 4;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidIndexCount,
				"Incomplete triangle indices fail instead of truncating a primitive");
			input = Triangle();
			input.m_Indices.m_SizeInBytes = 11;
			context.Check(validate(input).m_Error == RHIASValidationError::InvalidBufferRange,
				"The complete index stream must lie inside the declared slice");
			input = Triangle();
			input.m_IndexCount = 6;
			input.m_Indices.m_SizeInBytes = 24;
			auto limits = Limits;
			limits.m_MaxPrimitiveCount = 1;
			const auto capacity = ValidateRHIASIndexedTriangles(input, GeometryBuffer, GeometryBuffer, limits);
			context.Check(capacity.m_Error == RHIASValidationError::PrimitiveLimitExceeded,
				"Primitive capacity failure cannot silently drop geometry");
			auto hostBuffer = GeometryBuffer;
			hostBuffer.m_MemoryUsage = RHIMemoryUsage::CpuToGpu;
			const auto memory = ValidateRHIASIndexedTriangles(Triangle(), hostBuffer, GeometryBuffer, Limits);
			context.Check(memory.m_Error == RHIASValidationError::InvalidMemoryUsage,
				"Immutable geometry pools cannot alias CPU-rewritable upload storage");
			limits = Limits;
			limits.m_ScratchAlignmentInBytes = 3;
			const auto alignment = ValidateRHIASIndexedTriangles(Triangle(), GeometryBuffer, GeometryBuffer, limits);
			context.Check(alignment.m_Error == RHIASValidationError::InvalidLimits,
				"Malformed backend limits fail before a native build");
			context.Check(ValidateRHIASBuildPreference(RHIASBuildPreference::FastTrace).IsValid() &&
				ValidateRHIASBuildPreference(RHIASBuildPreference::FastBuild).IsValid() &&
				!ValidateRHIASBuildPreference(static_cast<RHIASBuildPreference>(255)).IsValid(),
				"Build policy accepts the two full-build preferences without inventing an update mode");
		}

		void RunInstanceTests(SelfTestContext& context) noexcept
		{
			std::array instances
			{
				RHIASInstanceDesc{ .m_BottomLevel = { 0, 1 } },
				RHIASInstanceDesc{ .m_BottomLevel = { 1, 1 }, .m_InstanceIndex = 0xffffff, .m_Mask = 0 },
			};
			instances[0].m_Transform = { -2, 0, 0, 12, 0, 3, 0, -4, 0, 0, 4, 8 };
			instances[0].m_Flags = RHIASInstanceFlags::TriangleCullDisable |
				RHIASInstanceFlags::TriangleFrontCounterClockwise;
			context.Check(ValidateRHIASInstances(instances, Limits).IsValid(),
				"Mirrored affine transforms, 24-bit IDs and mask-zero BLEND entries are legal");
			const RHIASBuildInputs bottom{ .m_Input = Triangle() };
			const RHIASBuildInputs top{ .m_Input = std::span<const RHIASInstanceDesc>{ instances } };
			context.Check(bottom.GetType() == RHIAccelerationStructureType::BottomLevel &&
				top.GetType() == RHIAccelerationStructureType::TopLevel,
				"Disjoint build inputs determine BLAS versus TLAS without inconsistent type labels");
			auto invalid = instances;
			invalid[1].m_InstanceIndex = 0x1000000;
			context.Check(ValidateRHIASInstances(invalid, Limits).m_Error ==
				RHIASValidationError::InvalidInstanceIndex,
				"Instance IDs must fit both backends without high-bit truncation");
			invalid = instances;
			invalid[1].m_BottomLevel.Reset();
			context.Check(ValidateRHIASInstances(invalid, Limits).m_Error ==
				RHIASValidationError::InvalidBottomLevel,
				"Even masked entries require a valid logical BLAS reference");
			invalid = instances;
			invalid[1].m_Flags = static_cast<RHIASInstanceFlags>(0x80);
			context.Check(ValidateRHIASInstances(invalid, Limits).m_Error ==
				RHIASValidationError::InvalidInstanceFlags,
				"Unknown native instance bits cannot enter the neutral contract");
			for (const float bad : { 0.0f, std::numeric_limits<float>::infinity(),
				std::numeric_limits<float>::quiet_NaN() })
			{
				invalid = instances;
				invalid[1].m_Transform[0] = bad;
				context.Check(ValidateRHIASInstances(invalid, Limits).m_Error ==
					RHIASValidationError::InvalidTransform,
					"Singular and non-finite instance transforms fail before native encoding");
			}
			auto limits = Limits;
			limits.m_MaxInstanceCount = 1;
			context.Check(ValidateRHIASInstances(instances, limits).m_Error ==
				RHIASValidationError::InstanceLimitExceeded &&
				ValidateRHIASInstances({}, Limits).m_Error == RHIASValidationError::InvalidInstanceCount,
				"Instance overflow and an empty TLAS report failure without truncation");
		}

		void RunScratchTests(SelfTestContext& context) noexcept
		{
			RHIBufferDesc buffer{
				.m_SizeInBytes = 1024,
				.m_Usage = RHIBufferUsage::UnorderedAccess,
			};
			RHIASBufferRange scratch{ RHIBufferHandle{ 2, 1 }, 256, 256 };
			const RHIASBuildSizes sizes{ 1024, 256 };
			context.Check(ValidateRHIASScratch(scratch, buffer, sizes, Limits).IsValid(),
				"GPU-only scratch covers the queried full-build requirement at backend alignment");
			scratch.m_OffsetInBytes = 128;
			context.Check(ValidateRHIASScratch(scratch, buffer, sizes, Limits).m_Error ==
				RHIASValidationError::InvalidScratchAlignment,
				"An otherwise in-bounds scratch slice must satisfy the device's alignment");
			scratch.m_OffsetInBytes = 256;
			scratch.m_SizeInBytes = 255;
			context.Check(ValidateRHIASScratch(scratch, buffer, sizes, Limits).m_Error ==
				RHIASValidationError::InvalidBuildSizes,
				"Insufficient scratch fails without shrinking the native build");
			scratch.m_SizeInBytes = 256;
			buffer.m_Usage = RHIBufferUsage::CopyDest;
			context.Check(ValidateRHIASScratch(scratch, buffer, sizes, Limits).m_Error ==
				RHIASValidationError::InvalidScratchUsage,
				"A copy-only buffer cannot act as build scratch");
			buffer.m_Usage = RHIBufferUsage::UnorderedAccess;
			buffer.m_CreateFlags = RHIBufferCreateFlags::UploadStorage;
			context.Check(ValidateRHIASScratch(scratch, buffer, sizes, Limits).m_Error ==
				RHIASValidationError::InvalidMemoryUsage,
				"Batch upload storage cannot be repurposed as GPU-written scratch");
		}

		void RunBindingTests(SelfTestContext& context) noexcept
		{
			const LayoutBindingRequirement tlas{
				.m_Key = LayoutBindingKey{ 0x9000 },
				.m_Slot = {
					.m_Type = RHIBindingType::AccelerationStructure,
					.m_Visibility = RHIShaderStage::Compute,
					.m_Binding = 8,
				},
				.m_ParameterOrder = 20,
			};
			const std::array rayBindings{ tlas };
			const auto forward = scene_bindings::ComposeForwardLayout(
				scene_bindings::GetCommonLayoutContract(), true);
			context.Check(forward.IsSuccess(), "The existing Forward+ contract remains available");
			if (!forward.m_Layout)
			{
				return;
			}
			const std::array fragments
			{
				LayoutFragment{ "Forward", forward.m_Layout->GetBindings() },
				LayoutFragment{ "Ray query", rayBindings },
			};
			const std::array reservations
			{
				LayoutBindingReservation{
					.m_Owner = tlas.m_Key, .m_Type = RHIBindingType::AccelerationStructure, .m_Binding = 8 },
			};
			const auto composed = ComposeLayoutFragments(fragments, reservations);
			context.Check(composed.IsSuccess() && composed.m_Layout->GetParameterCount() ==
				forward.m_Layout->GetParameterCount() + 1,
				"A fixed TLAS adds its own parameter alongside Forward+ and global heaps");
			if (!composed.m_Layout)
			{
				return;
			}
			const auto bindings = composed.m_Layout->GetBindings();
			const auto atmosphere = std::ranges::find(bindings, scene_bindings::AtmosphereConstants,
				&LayoutBindingRequirement::m_Key);
			context.Check(atmosphere != bindings.end() && atmosphere->m_Slot.m_Binding == 5 &&
				atmosphere->m_Slot.m_Type == RHIBindingType::ConstantBuffer,
				"TLAS composition preserves Atmosphere's independent b5 ownership");
			for (uint32_t variant = 0; variant < 5; ++variant)
			{
				auto changed = tlas;
				switch (variant)
				{
				case 0:
					changed.m_Slot.m_Count = 2;
					break;
				case 1:
					changed.m_Slot.m_Space = 1;
					break;
				case 2:
					changed.m_Slot.m_BufferBindingMode = RHIBufferBindingMode::Dynamic;
					break;
				case 3:
					changed.m_Slot.m_SizeInBytes = 64;
					break;
				default:
					changed.m_PayloadTag = LayoutPayloadTag{ 1 };
					break;
				}
				const std::array changedBindings{ changed };
				const std::array invalid{ LayoutFragment{ "Invalid TLAS", changedBindings } };
				constexpr std::array errors
				{
					LayoutCompositionErrorCode::InvalidCount,
					LayoutCompositionErrorCode::InvalidRegisterSpace,
					LayoutCompositionErrorCode::InvalidBufferMode,
					LayoutCompositionErrorCode::InvalidSize,
					LayoutCompositionErrorCode::InvalidPayloadType,
				};
				const auto result = ComposeLayoutFragments(invalid);
				context.Check(!result.m_Layout && result.m_Error.m_Code == errors[variant],
					"TLAS bindings reject arrays, global space, dynamic buffers and constant payload labels");
			}
			auto conflicting = tlas;
			conflicting.m_Key = LayoutBindingKey{ 0x9001 };
			conflicting.m_Slot.m_Type = RHIBindingType::ReadOnlyStorageBuffer;
			const std::array conflictBindings{ tlas, conflicting };
			const std::array conflict{ LayoutFragment{ "Read-only collision", conflictBindings } };
			context.Check(ComposeLayoutFragments(conflict).m_Error.m_Code ==
				LayoutCompositionErrorCode::RegisterOverlap,
				"TLAS and ordinary read-only resources share the HLSL t-register namespace");
			const std::array wrongOwnerBindings{ conflicting };
			const std::array wrongOwner{ LayoutFragment{ "Wrong owner", wrongOwnerBindings } };
			context.Check(ComposeLayoutFragments(wrongOwner, reservations).m_Error.m_Code ==
				LayoutCompositionErrorCode::ReservedBindingConflict,
				"A TLAS reservation cannot be taken by a different semantic resource");
			auto hitData = conflicting;
			hitData.m_Slot.m_Binding = 9;
			hitData.m_Slot.m_SizeInBytes = 32;
			hitData.m_PayloadTag = LayoutPayloadTag{ 90 };
			auto incompatibleHitData = hitData;
			incompatibleHitData.m_PayloadTag = LayoutPayloadTag{ 91 };
			const std::array hitBindings{ hitData, incompatibleHitData };
			const std::array hitFragments{ LayoutFragment{ "Hit data", hitBindings } };
			context.Check(ComposeLayoutFragments(hitFragments).m_Error.m_Code ==
				LayoutCompositionErrorCode::ConflictingBinding,
				"Equal-size hit-data declarations with incompatible payload identities cannot merge");

			const auto desc = composed.m_Layout->BuildRHIDesc("Ray query contract");
			DX12BindingLayoutBuildResult dx12{};
			BuildDX12RootSignatureDesc(desc, dx12);
			context.Check(dx12.IsValid() && std::ranges::any_of(
				std::span(dx12.m_RootParameters.data(), dx12.m_RootParameterCount), [](const auto& parameter)
				{
					return parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV &&
						parameter.Descriptor.ShaderRegister == 8 && parameter.Descriptor.RegisterSpace == 0;
				}),
				"DX12 lowers checked fixed TLAS slots as root SRVs");
			RHIBindingLayoutDesc oversized{};
			oversized.m_SlotCount = RHIBindingLayoutDesc::MaxSlots + 1;
			BuildDX12RootSignatureDesc(oversized, dx12);
			context.Check(!dx12.IsValid() && dx12.m_Error == DX12BindingLayoutError::TooManySlots,
				"Native layout preflight cannot scan beyond the fixed slot array");
#if GGLAB_ENABLE_VULKAN
			const auto vulkan = BuildVulkanBindingLayoutPlan(desc, 16 * 1024);
			context.Check(vulkan.IsValid() && std::ranges::any_of(
				std::span(vulkan.m_Set0Bindings.data(), vulkan.m_Set0BindingCount), [](const auto& binding)
				{
					return binding.m_BindingType == RHIBindingType::AccelerationStructure &&
						binding.m_DescriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
				}), "Vulkan keeps fixed TLAS descriptors distinct from storage-buffer descriptors");
#endif
		}
	}

	void RunRayQueryContractSelfTests(SelfTestContext& context) noexcept
	{
		RunDX12PipelineIdentityTests(context);
		RHIAccelerationStructureStore store;
		auto bottom = std::make_shared<RHIAccelerationStructureBackingBase>();
		std::weak_ptr<RHIAccelerationStructureBackingBase> weakBottom = bottom;
		const auto bottomHandle = store.Insert(bottom);
		auto top = std::make_shared<RHIAccelerationStructureBackingBase>();
		top->m_Type = RHIAccelerationStructureType::TopLevel;
		top->m_BottomLevels.push_back(bottom);
		const auto topHandle = store.Insert(top);
		bottom.reset();
		context.Check(store.Destroy(bottomHandle) && !store.Resolve(bottomHandle) && !weakBottom.expired(),
			"TLAS backing retains its BLAS after logical handle destruction");
		const auto replacement = store.Insert(std::make_shared<RHIAccelerationStructureBackingBase>());
		context.Check(replacement.Index() == bottomHandle.Index() && replacement.Generation() != bottomHandle.Generation() &&
			!store.Resolve(bottomHandle) && !store.Destroy(bottomHandle),
			"AS slot reuse cannot resolve or destroy a replacement through the old generation");
		auto recorded = store.Resolve(topHandle);
		top.reset();
		context.Check(store.Destroy(topHandle) && !weakBottom.expired(),
			"A recording lease retains the TLAS and its transitive native BLAS dependencies");
		recorded.reset();
		context.Check(weakBottom.expired(), "The last recording or submission lease releases retired AS backing");
		static_assert(!std::is_convertible_v<RHIAccelerationStructureHandle, RHIBufferHandle>);
		static_assert(!std::is_convertible_v<RHIBufferHandle, RHIAccelerationStructureHandle>);
		RunCapabilityTests(context);
		RunTriangleTests(context);
		RunInstanceTests(context);
		RunScratchTests(context);
		RunBindingTests(context);
	}
}
