#include "LayoutContractSelfTests.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/RenderParameters.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/Renderer.h"
#include "Graphics/RHI/DX12/Utility/DX12BindingLayoutUtils.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"
#endif

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] LayoutBindingRequirement MakeBinding(uint32_t key, RHIBindingType type,
			uint32_t binding, uint32_t order = 0) noexcept
		{
			return {
				.m_Key = LayoutBindingKey{ key },
				.m_Slot = {
					.m_Type = type,
					.m_Visibility = RHIShaderStage::All,
					.m_Binding = binding,
					.m_Count = IsBindlessBindingType(type) ? 0u : 1u,
					.m_SizeInBytes = type == RHIBindingType::InlineConstants ? 16u : 0u,
				},
				.m_ParameterOrder = order,
			};
		}

		[[nodiscard]] LayoutCompositionResult ComposeOne(const LayoutBindingRequirement& binding) noexcept
		{
			const std::array fragments{ LayoutFragment{ "Input", std::span{ &binding, 1 } } };
			return ComposeLayoutFragments(fragments);
		}

		void RunCompositionTests(SelfTestContext& context) noexcept
		{
			auto scene = MakeBinding(90, RHIBindingType::ConstantBuffer, 0, 0);
			scene.m_Slot.m_SizeInBytes = 128;
			scene.m_PayloadTag = LayoutPayloadTag{ 1 };
			auto atmosphere = MakeBinding(4, RHIBindingType::ConstantBuffer, 5, 2);
			atmosphere.m_Slot.m_SizeInBytes = 128;
			atmosphere.m_PayloadTag = LayoutPayloadTag{ 2 };
			const auto globals = MakeBinding(40, RHIBindingType::BindlessResourceTable, 0, 1);
			const std::array first{ scene, globals };
			const std::array second{ atmosphere };
			const std::array forward{ LayoutFragment{ "Scene", first }, LayoutFragment{ "Sky", second } };
			const std::array reverse{ forward[1], forward[0], forward[1] };
			const auto composed = ComposeLayoutFragments(forward);
			const auto reordered = ComposeLayoutFragments(reverse);
			context.Check(composed.IsSuccess() && reordered.IsSuccess() &&
				*composed.m_Layout == *reordered.m_Layout,
				"Fragment order and repeated identical fragments preserve logical composition");
			if (!composed.m_Layout)
			{
				return;
			}
			context.Check(composed.m_Layout->GetParameterCount() == 2 &&
				composed.m_Layout->FindParameterIndex(scene.m_Key) == 0u &&
				composed.m_Layout->FindParameterIndex(atmosphere.m_Key) == 1u &&
				!composed.m_Layout->FindParameterIndex(globals.m_Key) &&
				!composed.m_Layout->FindParameterIndex(LayoutBindingKey{ 999 }),
				"Semantic order is independent of key values and excludes bindless parameters");

			const std::array nested{ LayoutFragment{ "Preset", composed.m_Layout->GetBindings() }, forward[0] };
			const auto regrouped = ComposeLayoutFragments(nested);
			context.Check(regrouped.IsSuccess() && *regrouped.m_Layout == *composed.m_Layout,
				"A composed preset can be reused as a fragment without changing the contract");

			auto vertex = scene;
			vertex.m_Slot.m_Visibility = RHIShaderStage::Vertex;
			auto pixel = scene;
			pixel.m_Slot.m_Visibility = RHIShaderStage::Pixel;
			const std::array vertexBindings{ vertex };
			const std::array pixelBindings{ pixel };
			const std::array stages{ LayoutFragment{ "VS", vertexBindings }, LayoutFragment{ "PS", pixelBindings } };
			const auto merged = ComposeLayoutFragments(stages);
			context.Check(merged.IsSuccess() && merged.m_Layout->GetBindings().size() == 1 &&
				merged.m_Layout->GetBindings()[0].m_Slot.m_Visibility == RHIShaderStage::AllGraphics,
				"Identical semantic declarations union their stage visibility");

			const std::array emptyFragments{ LayoutFragment{} };
			const auto empty = ComposeLayoutFragments(emptyFragments);
			context.Check(empty.IsSuccess() && empty.m_Layout->GetBindings().empty() &&
				empty.m_Layout->GetParameterCount() == 0,
				"An explicitly empty composition has no bindings or native parameters");
		}

		void RunConflictTests(SelfTestContext& context) noexcept
		{
			auto original = MakeBinding(1, RHIBindingType::ConstantBuffer, 0);
			original.m_Slot.m_SizeInBytes = 128;
			original.m_PayloadTag = LayoutPayloadTag{ 1 };
			for (uint32_t field = 0; field < 7; ++field)
			{
				auto changed = original;
				switch (field)
				{
				case 0:
					changed.m_PayloadTag = LayoutPayloadTag{ 8 };
					break;
				case 1:
					changed.m_Slot.m_Type = RHIBindingType::ReadOnlyStorageBuffer;
					break;
				case 2:
					changed.m_Slot.m_Binding = 1;
					break;
				case 3:
					changed.m_Slot.m_Count = 2;
					break;
				case 4:
					changed.m_Slot.m_SizeInBytes = 112;
					break;
				case 5:
					changed.m_Slot.m_BufferBindingMode = RHIBufferBindingMode::Dynamic;
					break;
				default:
					changed.m_ParameterOrder = 9;
					break;
				}
				const std::array first{ original };
				const std::array second{ changed };
				const std::array fragments{ LayoutFragment{ "Original", first }, LayoutFragment{ "Changed", second } };
				const auto result = ComposeLayoutFragments(fragments);
				context.Check(!result.IsSuccess() && !result.m_Layout &&
					result.m_Error.m_Code == LayoutCompositionErrorCode::ConflictingBinding &&
					result.m_Error.m_First.m_FragmentName == "Original" &&
					result.m_Error.m_Second.m_FragmentName == "Changed" &&
					result.m_Error.m_First.m_Declaration.m_Key == original.m_Key &&
					result.m_Error.m_Second.m_Declaration.m_Key == changed.m_Key,
					"A same-key definition conflict retains both declarations and returns no partial layout");
			}

			const auto inlineAtScene = MakeBinding(2, RHIBindingType::InlineConstants, 0);
			const std::array overlapping{ original, inlineAtScene };
			const std::array overlapFragments{ LayoutFragment{ "Overlap", overlapping } };
			context.Check(ComposeLayoutFragments(overlapFragments).m_Error.m_Code ==
				LayoutCompositionErrorCode::RegisterOverlap,
				"Inline constants and constant buffers cannot share a b register");

			auto textures = MakeBinding(3, RHIBindingType::SampledTexture, 4);
			textures.m_Slot.m_Count = 3;
			const auto table = MakeBinding(4, RHIBindingType::ReadOnlyStorageBuffer, 6);
			const std::array arrayOverlap{ textures, table };
			const std::array arrayFragments{ LayoutFragment{ "Range", arrayOverlap } };
			context.Check(ComposeLayoutFragments(arrayFragments).m_Error.m_Code ==
				LayoutCompositionErrorCode::RegisterOverlap,
				"A texture array reserves its complete range in the shared t namespace");
			textures.m_Slot.m_Count = 2;
			const std::array adjacent{ textures, table, original };
			const std::array adjacentFragments{ LayoutFragment{ "Adjacent", adjacent } };
			context.Check(ComposeLayoutFragments(adjacentFragments).IsSuccess(),
				"Adjacent t ranges and independent register classes do not overlap");

			auto otherSpace = original;
			otherSpace.m_Key = LayoutBindingKey{ 5 };
			otherSpace.m_Slot.m_Space = 2;
			const std::array spaces{ original, otherSpace };
			const std::array spaceFragments{ LayoutFragment{ "Spaces", spaces } };
			context.Check(ComposeLayoutFragments(spaceFragments).IsSuccess(),
				"Logical composition distinguishes spaces while backend support remains a separate check");

			const std::array globalAlias{
				MakeBinding(8, RHIBindingType::BindlessResourceTable, 0),
				MakeBinding(9, RHIBindingType::BindlessResourceTable, 0) };
			const std::array globalFragments{ LayoutFragment{ "Globals", globalAlias } };
			context.Check(ComposeLayoutFragments(globalFragments).m_Error.m_Code ==
				LayoutCompositionErrorCode::DuplicateGlobalNamespace,
				"Different semantic keys cannot alias one global resource namespace");
		}

		void RunValidationTests(SelfTestContext& context) noexcept
		{
			const auto valid = MakeBinding(1, RHIBindingType::ConstantBuffer, 0);
			auto invalid = valid;
			invalid.m_Key.Reset();
			context.Check(ComposeOne(invalid).m_Error.m_Code == LayoutCompositionErrorCode::InvalidKey,
				"An invalid semantic key is rejected");
			invalid = valid;
			invalid.m_Slot.m_Type = static_cast<RHIBindingType>(255);
			context.Check(ComposeOne(invalid).m_Error.m_Code == LayoutCompositionErrorCode::InvalidType,
				"Unknown binding categories are rejected");
			for (const auto visibility : { RHIShaderStage::None, static_cast<RHIShaderStage>(8) })
			{
				invalid = valid;
				invalid.m_Slot.m_Visibility = visibility;
				context.Check(ComposeOne(invalid).m_Error.m_Code == LayoutCompositionErrorCode::InvalidVisibility,
					"Empty or unknown stage masks are rejected");
			}
			invalid = valid;
			invalid.m_Slot.m_Count = 0;
			context.Check(ComposeOne(invalid).m_Error.m_Code == LayoutCompositionErrorCode::InvalidCount,
				"A fixed binding cannot use the unbounded global count");
			invalid = MakeBinding(2, RHIBindingType::BindlessSamplerTable, 0);
			invalid.m_Slot.m_Count = 1;
			context.Check(ComposeOne(invalid).m_Error.m_Code == LayoutCompositionErrorCode::InvalidCount,
				"A global namespace must use its unbounded marker");
			invalid = valid;
			invalid.m_Slot.m_Binding = std::numeric_limits<uint32_t>::max();
			context.Check(ComposeOne(invalid).IsSuccess(),
				"The last representable register has a valid one-element range");
			invalid.m_Slot.m_Count = 2;
			context.Check(ComposeOne(invalid).m_Error.m_Code == LayoutCompositionErrorCode::RegisterRangeOverflow,
				"Register interval arithmetic cannot wrap at UINT32_MAX");
			for (const uint32_t size : { 0u, 6u })
			{
				invalid = MakeBinding(2, RHIBindingType::InlineConstants, 1);
				invalid.m_Slot.m_SizeInBytes = size;
				context.Check(ComposeOne(invalid).m_Error.m_Code == LayoutCompositionErrorCode::InvalidSize,
					"Inline transport capacity is nonempty and word aligned");
			}
			invalid = MakeBinding(2, RHIBindingType::ReadOnlyStorageBuffer, 0);
			invalid.m_Slot.m_BufferBindingMode = RHIBufferBindingMode::Dynamic;
			context.Check(ComposeOne(invalid).m_Error.m_Code == LayoutCompositionErrorCode::InvalidBufferMode,
				"Dynamic uniform policy cannot silently attach to a storage buffer");
			invalid = MakeBinding(2, RHIBindingType::SampledTexture, 0);
			invalid.m_PayloadTag = LayoutPayloadTag{ 7 };
			context.Check(ComposeOne(invalid).m_Error.m_Code == LayoutCompositionErrorCode::InvalidPayloadType,
				"A typed buffer payload tag cannot attach to a sampled texture");

			std::array<LayoutBindingRequirement, RHIBindingLayoutDesc::MaxSlots + 1> manyBindings;
			for (uint32_t index = 0; index < manyBindings.size(); ++index)
			{
				manyBindings[index] = MakeBinding(index, RHIBindingType::ConstantBuffer, index);
			}
			const std::array atLimit{ LayoutFragment{ "Full", std::span(manyBindings).first(32) } };
			context.Check(ComposeLayoutFragments(atLimit).IsSuccess(),
				"The existing RHI slot capacity is accepted exactly");
			const std::array overflow{ atLimit[0], LayoutFragment{ "Extra", std::span(manyBindings).last(1) } };
			context.Check(ComposeLayoutFragments(overflow).m_Error.m_Code == LayoutCompositionErrorCode::TooManyBindings,
				"Unique output bindings cannot exceed the RHI slot capacity");
			std::array<LayoutFragment, MaxLayoutFragments + 1> manyFragments{};
			context.Check(ComposeLayoutFragments(manyFragments).m_Error.m_Code == LayoutCompositionErrorCode::TooManyFragments,
				"Fragment input count is bounded before traversal");
		}

		void RunReservationAndOwnershipTests(SelfTestContext& context) noexcept
		{
			const LayoutBindingReservation reservation{ LayoutBindingKey{ 31 }, RHIBindingType::ReadWriteStorageBuffer, 31 };
			const std::array reservations{ reservation };
			auto counter = MakeBinding(31, RHIBindingType::ReadWriteStorageBuffer, 31);
			const std::array counters{ counter };
			const std::array fragments{ LayoutFragment{ "Counter", counters } };
			context.Check(ComposeLayoutFragments(fragments, reservations).IsSuccess(),
				"A reserved observation register accepts its exact owner");
			auto intruder = MakeBinding(32, RHIBindingType::StorageTexture, 31);
			const std::array intruders{ intruder };
			const std::array conflicting{ LayoutFragment{ "Intruder", intruders } };
			const auto rejected = ComposeLayoutFragments(conflicting, reservations);
			context.Check(rejected.m_Error.m_Code == LayoutCompositionErrorCode::ReservedBindingConflict &&
				rejected.m_Error.m_Second.m_Declaration.m_Key == reservation.m_Owner,
				"A different UAV category cannot occupy reserved observation storage");
			counter.m_Slot.m_Binding = 30;
			const std::array misplaced{ counter };
			const std::array misplacedFragments{ LayoutFragment{ "Moved counter", misplaced } };
			context.Check(ComposeLayoutFragments(misplacedFragments, reservations).m_Error.m_Code ==
				LayoutCompositionErrorCode::ReservedBindingConflict,
				"A reservation owner cannot silently move to another register");
			auto aliasedReservation = reservation;
			aliasedReservation.m_Owner = LayoutBindingKey{ 32 };
			const std::array invalidReservations{ reservation, aliasedReservation };
			context.Check(ComposeLayoutFragments({}, invalidReservations).m_Error.m_Code ==
				LayoutCompositionErrorCode::InvalidReservation,
				"Conflicting reservation owners reject even before any binding uses the address");

			const auto owned = []
				{
					std::string name = "Owned scene debug name";
					auto binding = MakeBinding(1, RHIBindingType::ConstantBuffer, 0);
					binding.m_Slot.m_DebugName = name.c_str();
					return ComposeOne(binding);
				}();
			const auto copied = owned;
			const char* debugName = copied.m_Layout ? copied.m_Layout->BuildRHIDesc().m_Slots[0].m_DebugName : nullptr;
			context.Check(debugName && std::string_view(debugName) == "Owned scene debug name" &&
				copied.m_Layout->GetBindings()[0].m_Slot.m_DebugName == nullptr,
				"Composed copies own debug labels without retaining input pointers");
		}

		void RunSceneFamilyTests(SelfTestContext& context) noexcept
		{
			const auto& common = scene_bindings::GetCommonLayoutContract();
			const auto desc = common.BuildRHIDesc();
			constexpr std::array legacyKeys{
				scene_bindings::SceneConstants, scene_bindings::DrawParameters,
				scene_bindings::PassParameters, scene_bindings::ObjectTable,
				scene_bindings::MaterialTable, scene_bindings::ViewTable,
				scene_bindings::LightTable, scene_bindings::InlineParameters,
			};
			constexpr std::array legacyTypes{
				RHIBindingType::ConstantBuffer, RHIBindingType::InlineConstants,
				RHIBindingType::ConstantBuffer, RHIBindingType::ReadOnlyStorageBuffer,
				RHIBindingType::ReadOnlyStorageBuffer, RHIBindingType::ReadOnlyStorageBuffer,
				RHIBindingType::ReadOnlyStorageBuffer, RHIBindingType::InlineConstants,
				RHIBindingType::BindlessResourceTable, RHIBindingType::BindlessSamplerTable,
			};
			constexpr std::array legacyRegisters{ 0u, 1u, 2u, 1u, 2u, 3u, 4u, 4u, 0u, 0u };
			constexpr std::array legacyNames{
				"SceneCB", "DrawConstants", "PassCB", "ObjectSB", "MaterialSB",
				"ViewSB", "LightSB", "InlineConstants", "BindlessResources", "BindlessSamplers",
			};
			context.Check(desc.m_SlotCount == 10 && common.GetParameterCount() == 8,
				"The common preset retains its ten slots and eight ordinary parameters");
			for (uint32_t index = 0; index < legacyTypes.size(); ++index)
			{
				const auto& slot = desc.m_Slots[index];
				const uint32_t size = index == 1 ?
					MaxDrawConstantDWORDs * sizeof(uint32_t) :
					index == 7 ? MaxInlineConstantDWORDs * sizeof(uint32_t) : 0u;
				context.Check(slot.m_Type == legacyTypes[index] && slot.m_Binding == legacyRegisters[index] &&
					slot.m_Space == 0 && slot.m_Visibility == RHIShaderStage::All &&
					slot.m_Count == (index < 8 ? 1u : 0u) && slot.m_SizeInBytes == size &&
					slot.m_BufferBindingMode == (index == 2 ? RHIBufferBindingMode::Dynamic : RHIBufferBindingMode::Fixed) &&
					slot.m_DebugName && std::string_view(slot.m_DebugName) == legacyNames[index] &&
					(index >= legacyKeys.size() || common.FindParameterIndex(legacyKeys[index]) == index),
					"Every common slot retains its pre-composition register, policy, label and legacy index");
			}
			for (uint32_t family = 0; family < 3; ++family)
			{
				const auto forward = scene_bindings::ComposeForwardLayout(common, family == 2);
				context.Check(forward.IsSuccess(), "The bounded Forward family composes from the common preset");
				if (!forward.m_Layout)
				{
					continue;
				}
				const auto& contract = family == 0 ? common : *forward.m_Layout;
				const auto base = contract.BuildRHIDesc();
				const uint32_t parameterCount = family == 0 ? 8u : family == 1 ? 9u : 11u;
				bool preserved = base.m_SlotCount == parameterCount + 2;
				for (uint32_t index = 0; index < legacyKeys.size(); ++index)
				{
					preserved &= contract.FindParameterIndex(legacyKeys[index]) == index &&
						base.m_Slots[index].m_DebugName &&
						std::string_view(base.m_Slots[index].m_DebugName) == legacyNames[index];
				}
				context.Check(preserved && (family == 0 ||
					contract.FindParameterIndex(scene_bindings::DirectionalShadow) == 8u) &&
					(family != 2 || (contract.FindParameterIndex(scene_bindings::ForwardPlusTileHeaders) == 9u &&
						contract.FindParameterIndex(scene_bindings::ForwardPlusTileIndices) == 10u)),
					"Common, Forward and Forward+ preserve base maps and resolve their semantic extensions");

				DX12BindingLayoutBuildResult dx12{};
				BuildDX12RootSignatureDesc(base, dx12);
				context.Check(dx12.m_RootParameterCount == parameterCount &&
					(family == 0 || dx12.m_RootParameters[8].Descriptor.ShaderRegister == 3),
					"DX12 layouts retain the common parameters and semantic extension positions");
#if GGLAB_ENABLE_VULKAN
				const auto vulkan = BuildVulkanBindingLayoutPlan(base, 16 * 1024);
				context.Check(vulkan.IsValid() && vulkan.m_Set0BindingCount == parameterCount,
					"Vulkan layouts preserve the common and extension parameter count");
#endif
			}
		}

		void RunResolvedLayoutTests(SelfTestContext& context) noexcept
		{
			// Models native deduplication and the backend descriptor entry being overwritten.
			class SharedNativePipelineSystem final : public RHIPipelineSystem
			{
			public:
				RHIBindingLayoutHandle CreateBindingLayout(const RHIBindingLayoutDesc& desc) noexcept override
				{
					m_LastDesc = desc;
					m_LastDesc.m_DebugName = nullptr;
					for (auto& slot : m_LastDesc.m_Slots)
					{
						slot.m_DebugName = nullptr;
					}
					return m_Fail ? RHIBindingLayoutHandle{} : RHIBindingLayoutHandle{ 7, 1 };
				}
				bool GetBindingLayoutDesc(RHIBindingLayoutHandle, RHIBindingLayoutDesc& desc) const noexcept override
				{
					desc = m_LastDesc;
					return true;
				}
				RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineCreateInfo&) noexcept override { return {}; }
				RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineCreateInfo&) noexcept override { return {}; }
				bool IsAlive(RHIBindingLayoutHandle handle) const noexcept override { return handle.IsValid(); }
				bool IsAlive(RHIPipelineHandle handle) const noexcept override { return handle.IsValid(); }
				uint64_t GetRevision() const noexcept override { return 1; }
				void Clear() noexcept override {}
				RHIBindingLayoutDesc m_LastDesc{};
				bool m_Fail = false;
			} pipelineSystem;
			const auto first = [&pipelineSystem]
				{
					auto scene = MakeBinding(1, RHIBindingType::ConstantBuffer, 0);
					scene.m_PayloadTag = LayoutPayloadTag{ 1 };
					const auto composed = ComposeOne(scene);
					return ResolvedRenderLayout::Create(pipelineSystem, *composed.m_Layout);
				}();
			auto atmosphere = MakeBinding(2, RHIBindingType::ConstantBuffer, 0);
			atmosphere.m_PayloadTag = LayoutPayloadTag{ 2 };
			const auto composed = ComposeOne(atmosphere);
			const auto second = ResolvedRenderLayout::Create(pipelineSystem, *composed.m_Layout);
			context.Check(first.IsValid() && second.IsValid() &&
				first.GetNativeLayout() == second.GetNativeLayout() &&
				!(*first.GetContract() == *second.GetContract()) &&
				first.GetContract()->FindParameterIndex(LayoutBindingKey{ 1 }) == 0u &&
				!first.GetContract()->FindParameterIndex(LayoutBindingKey{ 2 }) &&
				second.GetContract()->FindParameterIndex(LayoutBindingKey{ 2 }) == 0u &&
				!second.GetContract()->FindParameterIndex(LayoutBindingKey{ 1 }),
				"Two logical meanings keep independent owned maps when the backend reuses one native handle");
			pipelineSystem.m_Fail = true;
			const auto failed = ResolvedRenderLayout::Create(pipelineSystem, *composed.m_Layout);
			context.Check(!failed.IsValid() && !failed.GetContract() && !failed.GetNativeLayout().IsValid(),
				"Native creation failure publishes neither a logical map nor a native handle");
		}

		void RunBackendPlanTests(SelfTestContext& context) noexcept
		{
			const auto common = Renderer::BuildCommonRHIBindingLayoutDesc();
			std::array<LayoutBindingRequirement, RHIBindingLayoutDesc::MaxSlots> requirements{};
			for (uint32_t index = 0; index < common.m_SlotCount; ++index)
			{
				requirements[index] = {
					.m_Key = LayoutBindingKey{ 100 - index },
					.m_Slot = common.m_Slots[index],
					.m_ParameterOrder = index,
				};
			}
			const std::array commonFragments{ LayoutFragment{ "Common", std::span(requirements).first(common.m_SlotCount) } };
			const auto reconstructed = ComposeLayoutFragments(commonFragments);
			context.Check(reconstructed.IsSuccess() && reconstructed.m_Layout->GetParameterCount() == 8,
				"The implemented common layout reconstructs all eight native parameters");
			if (!reconstructed.m_Layout)
			{
				return;
			}
			auto shadow = MakeBinding(7, RHIBindingType::ConstantBuffer, 3, 20);
			shadow.m_Slot.m_SizeInBytes = 112;
			auto atmosphere = MakeBinding(5, RHIBindingType::ConstantBuffer, 5, 21);
			atmosphere.m_Slot.m_SizeInBytes = 128;
			const std::array extensions{ shadow, atmosphere };
			const std::array fragments{ LayoutFragment{ "Extension", extensions }, commonFragments[0] };
			const auto result = ComposeLayoutFragments(fragments);
			context.Check(result.IsSuccess() && result.m_Layout->FindParameterIndex(shadow.m_Key) == 8u &&
				result.m_Layout->FindParameterIndex(atmosphere.m_Key) == 9u,
				"Scene, Shadow and Atmosphere compose without disturbing legacy common parameters");
			if (!result.m_Layout)
			{
				return;
			}
			const auto desc = result.m_Layout->BuildRHIDesc();
			DX12BindingLayoutBuildResult dx12{};
			BuildDX12RootSignatureDesc(desc, dx12);
			context.Check(dx12.m_RootParameterCount == 10 &&
				dx12.m_RootParameters[0].Descriptor.ShaderRegister == 0 &&
				dx12.m_RootParameters[8].Descriptor.ShaderRegister == 3 &&
				dx12.m_RootParameters[9].Descriptor.ShaderRegister == 5,
				"The existing DX12 planner consumes composed Scene/Shadow/Atmosphere addresses");
#if GGLAB_ENABLE_VULKAN
			const auto vulkan = BuildVulkanBindingLayoutPlan(desc, 16 * 1024);
			const auto bindings = std::span(vulkan.m_Set0Bindings).first(vulkan.m_Set0BindingCount);
			const auto atmosphereSlot = std::ranges::find(bindings, 9u,
				&VulkanSet0BindingPlan::m_LogicalParameterIndex);
			context.Check(vulkan.IsValid() && vulkan.m_Set0BindingCount == 10 &&
				atmosphereSlot != bindings.end() && atmosphereSlot->m_Binding == 5,
				"The existing Vulkan planner preserves the composed logical-to-native Atmosphere mapping");
#endif
		}
	}

	void RunLayoutContractSelfTests(SelfTestContext& context) noexcept
	{
		RunCompositionTests(context);
		RunConflictTests(context);
		RunValidationTests(context);
		RunReservationAndOwnershipTests(context);
		RunBackendPlanTests(context);
		RunSceneFamilyTests(context);
		RunResolvedLayoutTests(context);
	}
}
