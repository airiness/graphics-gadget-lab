#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneConstantBindings.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RenderParameters.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"

#include <array>
#include <span>
#include <utility>

namespace gglab::scene_bindings
{
	namespace
	{
		[[nodiscard]] constexpr LayoutBindingRequirement Binding(LayoutBindingKey key,
			RHIBindingType type, uint32_t address, uint32_t order, const char* name,
			RHIShaderStage visibility = RHIShaderStage::All, uint32_t size = 0,
			RHIBufferBindingMode mode = RHIBufferBindingMode::Fixed, LayoutPayloadTag payload = {}) noexcept
		{
			return {
				.m_Key = key,
				.m_Slot = {
					.m_Type = type, .m_Visibility = visibility, .m_Binding = address,
					.m_Count = IsBindlessBindingType(type) ? 0u : 1u, .m_SizeInBytes = size,
					.m_DebugName = name, .m_BufferBindingMode = mode,
				},
				.m_PayloadTag = payload,
				.m_ParameterOrder = order,
			};
		}

		// Order preserves both the legacy native parameters and the descriptor slot sequence.
		constexpr std::array CommonBindings
		{
			Binding(SceneConstants, RHIBindingType::ConstantBuffer, 0, 0, "SceneCB",
				RHIShaderStage::All, 0, RHIBufferBindingMode::Fixed, ConstantPayloadTraits<SceneGPU>::Tag),
			Binding(DrawParameters, RHIBindingType::InlineConstants, 1, 1, "DrawConstants",
				RHIShaderStage::All, MaxDrawConstantDWORDs * sizeof(uint32_t)),
			Binding(PassParameters, RHIBindingType::ConstantBuffer, 2, 2, "PassCB",
				RHIShaderStage::All, 0, RHIBufferBindingMode::Dynamic),
			Binding(ObjectTable, RHIBindingType::ReadOnlyStorageBuffer, 1, 3, "ObjectSB"),
			Binding(MaterialTable, RHIBindingType::ReadOnlyStorageBuffer, 2, 4, "MaterialSB"),
			Binding(ViewTable, RHIBindingType::ReadOnlyStorageBuffer, 3, 5, "ViewSB"),
			Binding(LightTable, RHIBindingType::ReadOnlyStorageBuffer, 4, 6, "LightSB"),
			Binding(InlineParameters, RHIBindingType::InlineConstants, 4, 7, "InlineConstants",
				RHIShaderStage::All, MaxInlineConstantDWORDs * sizeof(uint32_t)),
			Binding(GlobalResources, RHIBindingType::BindlessResourceTable, 0, 8, "BindlessResources"),
			Binding(GlobalSamplers, RHIBindingType::BindlessSamplerTable, 0, 9, "BindlessSamplers"),
		};
		constexpr std::array ShadowBindings
		{
			Binding(DirectionalShadow, RHIBindingType::ConstantBuffer, 3, 10,
				"DirectionalShadowCB", RHIShaderStage::Pixel, 0, RHIBufferBindingMode::Fixed,
				ConstantPayloadTraits<DirectionalShadowGPU>::Tag),
		};
		constexpr std::array AtmosphereBindings
		{
			Binding(AtmosphereConstants, RHIBindingType::ConstantBuffer, 5, 13,
				"AtmosphereCB", RHIShaderStage::All, 0, RHIBufferBindingMode::Fixed,
				ConstantPayloadTraits<AtmosphereGPU>::Tag),
		};
		constexpr std::array TileBindings
		{
			Binding(ForwardPlusTileHeaders, RHIBindingType::ReadOnlyStorageBuffer, 5, 11,
				"ForwardPlusTileHeaders", RHIShaderStage::Pixel),
			Binding(ForwardPlusTileIndices, RHIBindingType::ReadOnlyStorageBuffer, 6, 12,
				"ForwardPlusTileIndices", RHIShaderStage::Pixel),
		};
		constexpr std::array Reservations
		{
			LayoutBindingReservation{ NonFiniteCounters,
				RHIBindingType::ReadWriteStorageBuffer, nonfinite::CounterBinding },
		};
	}

	const ComposedLayout& GetCommonLayoutContract() noexcept
	{
		static const ComposedLayout common = []
			{
				const std::array fragments{ LayoutFragment{ "Common", CommonBindings } };
				auto result = ComposeLayoutFragments(fragments, Reservations);
				GGLAB_ASSERT_MSG(result.IsSuccess(), "The common layout contract must compose.");
				return std::move(result.m_Layout).value();
			}();
		return common;
	}

	LayoutCompositionResult ComposeAtmosphereLayout(const ComposedLayout& common) noexcept
	{
		// Rematerialize borrowed debug labels while retaining the owned semantic declarations.
		const auto commonDesc = common.BuildRHIDesc();
		std::array<LayoutBindingRequirement, RHIBindingLayoutDesc::MaxSlots> commonBindings{};
		for (uint32_t index = 0; index < commonDesc.m_SlotCount; ++index)
		{
			commonBindings[index] = common.GetBindings()[index];
			commonBindings[index].m_Slot.m_DebugName = commonDesc.m_Slots[index].m_DebugName;
		}
		const std::array fragments{
			LayoutFragment{ "Common", std::span(commonBindings).first(commonDesc.m_SlotCount) },
			LayoutFragment{ "Atmosphere", AtmosphereBindings },
		};
		return ComposeLayoutFragments(fragments, Reservations);
	}

	LayoutCompositionResult ComposeForwardLayout(
		const ComposedLayout& common, bool forwardPlusLightLists) noexcept
	{
		// Rematerialize borrowed debug labels while retaining the owned semantic declarations.
		const auto commonDesc = common.BuildRHIDesc();
		std::array<LayoutBindingRequirement, RHIBindingLayoutDesc::MaxSlots> commonBindings{};
		for (uint32_t index = 0; index < commonDesc.m_SlotCount; ++index)
		{
			commonBindings[index] = common.GetBindings()[index];
			commonBindings[index].m_Slot.m_DebugName = commonDesc.m_Slots[index].m_DebugName;
		}
		const std::array fragments{
			LayoutFragment{ "Common", std::span(commonBindings).first(commonDesc.m_SlotCount) },
			LayoutFragment{ "DirectionalShadow", ShadowBindings },
			LayoutFragment{ "Atmosphere", AtmosphereBindings },
			LayoutFragment{ "ForwardPlus", forwardPlusLightLists ?
				std::span<const LayoutBindingRequirement>(TileBindings) :
				std::span<const LayoutBindingRequirement>{} },
		};
		return ComposeLayoutFragments(fragments, Reservations);
	}
}
