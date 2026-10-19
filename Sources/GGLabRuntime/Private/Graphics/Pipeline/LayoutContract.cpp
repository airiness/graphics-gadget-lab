#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		enum class RegisterClass : uint8_t
		{
			None,
			Constant,
			ReadOnly,
			ReadWrite,
			Sampler,
		};

		[[nodiscard]] RegisterClass GetRegisterClass(RHIBindingType type) noexcept
		{
			switch (type)
			{
			case RHIBindingType::ConstantBuffer:
			case RHIBindingType::InlineConstants:
				return RegisterClass::Constant;
			case RHIBindingType::ReadOnlyStorageBuffer:
			case RHIBindingType::SampledTexture:
				return RegisterClass::ReadOnly;
			case RHIBindingType::ReadWriteStorageBuffer:
			case RHIBindingType::StorageTexture:
				return RegisterClass::ReadWrite;
			case RHIBindingType::Sampler:
				return RegisterClass::Sampler;
			default:
				return RegisterClass::None;
			}
		}

		[[nodiscard]] bool IsBufferType(RHIBindingType type) noexcept
		{
			return type == RHIBindingType::ConstantBuffer ||
				type == RHIBindingType::InlineConstants ||
				type == RHIBindingType::ReadOnlyStorageBuffer ||
				type == RHIBindingType::ReadWriteStorageBuffer;
		}

		[[nodiscard]] bool IsRangeValid(uint32_t binding, uint32_t count) noexcept
		{
			return count != 0 && static_cast<uint64_t>(binding) + count <=
				static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1;
		}

		[[nodiscard]] bool Overlaps(const RHIBindingSlotDesc& first,
			const RHIBindingSlotDesc& second) noexcept
		{
			return first.m_Space == second.m_Space &&
				GetRegisterClass(first.m_Type) == GetRegisterClass(second.m_Type) &&
				static_cast<uint64_t>(first.m_Binding) <
					static_cast<uint64_t>(second.m_Binding) + second.m_Count &&
				static_cast<uint64_t>(second.m_Binding) <
					static_cast<uint64_t>(first.m_Binding) + first.m_Count;
		}

		[[nodiscard]] bool SameDefinition(const LayoutBindingRequirement& first,
			const LayoutBindingRequirement& second) noexcept
		{
			const auto& lhs = first.m_Slot;
			const auto& rhs = second.m_Slot;
			return first.m_Key == second.m_Key && first.m_PayloadTag == second.m_PayloadTag &&
				first.m_ParameterOrder == second.m_ParameterOrder && lhs.m_Type == rhs.m_Type &&
				lhs.m_Binding == rhs.m_Binding && lhs.m_Space == rhs.m_Space &&
				lhs.m_Count == rhs.m_Count && lhs.m_SizeInBytes == rhs.m_SizeInBytes &&
				lhs.m_BufferBindingMode == rhs.m_BufferBindingMode;
		}

		[[nodiscard]] LayoutCompositionErrorCode Validate(
			const LayoutBindingRequirement& binding) noexcept
		{
			const auto& slot = binding.m_Slot;
			if (!binding.m_Key.IsValid())
			{
				return LayoutCompositionErrorCode::InvalidKey;
			}
			if (!IsBindlessBindingType(slot.m_Type) && GetRegisterClass(slot.m_Type) == RegisterClass::None)
			{
				return LayoutCompositionErrorCode::InvalidType;
			}
			const auto visibility = static_cast<uint32_t>(slot.m_Visibility);
			if (visibility == 0 || (visibility & ~static_cast<uint32_t>(RHIShaderStage::All)) != 0)
			{
				return LayoutCompositionErrorCode::InvalidVisibility;
			}
			if (IsBindlessBindingType(slot.m_Type))
			{
				if (slot.m_Count != 0)
				{
					return LayoutCompositionErrorCode::InvalidCount;
				}
			}
			else if (slot.m_Count == 0)
			{
				return LayoutCompositionErrorCode::InvalidCount;
			}
			else if (!IsRangeValid(slot.m_Binding, slot.m_Count))
			{
				return LayoutCompositionErrorCode::RegisterRangeOverflow;
			}
			if ((slot.m_Type == RHIBindingType::InlineConstants &&
				(slot.m_Count != 1 || slot.m_SizeInBytes == 0 || slot.m_SizeInBytes % 4 != 0)) ||
				(slot.m_Type == RHIBindingType::ConstantBuffer && slot.m_SizeInBytes % 4 != 0) ||
				(!IsBufferType(slot.m_Type) && slot.m_SizeInBytes != 0))
			{
				return LayoutCompositionErrorCode::InvalidSize;
			}
			if ((slot.m_BufferBindingMode != RHIBufferBindingMode::Fixed &&
				slot.m_BufferBindingMode != RHIBufferBindingMode::Dynamic) ||
				(slot.m_BufferBindingMode == RHIBufferBindingMode::Dynamic &&
					slot.m_Type != RHIBindingType::ConstantBuffer))
			{
				return LayoutCompositionErrorCode::InvalidBufferMode;
			}
			if (binding.m_PayloadTag.IsValid() && !IsBufferType(slot.m_Type))
			{
				return LayoutCompositionErrorCode::InvalidPayloadType;
			}
			return LayoutCompositionErrorCode::None;
		}

		struct Declaration
		{
			LayoutBindingRequirement m_Binding;
			std::string m_DebugName;
			std::string m_FragmentName;
		};

		[[nodiscard]] Declaration OwnDeclaration(const LayoutBindingRequirement& binding,
			std::string_view fragmentName)
		{
			Declaration result{
				binding,
				binding.m_Slot.m_DebugName ? binding.m_Slot.m_DebugName : "",
				std::string(fragmentName),
			};
			result.m_Binding.m_Slot.m_DebugName = nullptr;
			return result;
		}

		[[nodiscard]] LayoutCompositionResult Fail(LayoutCompositionErrorCode code,
			const Declaration& first = {}, const Declaration& second = {}) noexcept
		{
			return {
				.m_Error = {
					.m_Code = code,
					.m_First = { first.m_FragmentName, first.m_Binding },
					.m_Second = { second.m_FragmentName, second.m_Binding },
				},
			};
		}
	}

	std::span<const LayoutBindingRequirement> ComposedLayout::GetBindings() const noexcept
	{
		return { m_Bindings.data(), m_BindingCount };
	}

	std::optional<uint32_t> ComposedLayout::FindParameterIndex(LayoutBindingKey key) const noexcept
	{
		for (uint32_t index = 0; index < m_BindingCount; ++index)
		{
			if (m_Bindings[index].m_Key == key && !IsBindlessBindingType(m_Bindings[index].m_Slot.m_Type))
			{
				return m_ParameterIndices[index];
			}
		}
		return std::nullopt;
	}

	RHIBindingLayoutDesc ComposedLayout::BuildRHIDesc(const char* debugName) const noexcept
	{
		RHIBindingLayoutDesc result{};
		result.m_DebugName = debugName;
		result.m_SlotCount = m_BindingCount;
		for (uint32_t index = 0; index < m_BindingCount; ++index)
		{
			result.m_Slots[index] = m_Bindings[index].m_Slot;
			result.m_Slots[index].m_DebugName = m_DebugNames[index].empty() ?
				nullptr :
				m_DebugNames[index].c_str();
		}
		return result;
	}

	bool ComposedLayout::operator==(const ComposedLayout& other) const noexcept
	{
		if (m_BindingCount != other.m_BindingCount)
		{
			return false;
		}
		for (uint32_t index = 0; index < m_BindingCount; ++index)
		{
			if (!SameDefinition(m_Bindings[index], other.m_Bindings[index]) ||
				m_Bindings[index].m_Slot.m_Visibility != other.m_Bindings[index].m_Slot.m_Visibility)
			{
				return false;
			}
		}
		return true;
	}

	ResolvedRenderLayout ResolvedRenderLayout::Create(RHIPipelineSystem& pipelineSystem,
		const ComposedLayout& contract, const char* debugName) noexcept
	{
		ResolvedRenderLayout result;
		const auto nativeLayout = pipelineSystem.CreateBindingLayout(contract.BuildRHIDesc(debugName));
		if (nativeLayout.IsValid())
		{
			result.m_Contract = std::make_shared<const ComposedLayout>(contract);
			result.m_NativeLayout = nativeLayout;
		}
		return result;
	}

	const ComposedLayout* ResolvedRenderLayout::GetContract() const noexcept
	{
		return m_Contract.get();
	}

	LayoutCompositionResult ComposeLayoutFragments(std::span<const LayoutFragment> fragments,
		std::span<const LayoutBindingReservation> reservations) noexcept
	{
		if (fragments.size() > MaxLayoutFragments)
		{
			return Fail(LayoutCompositionErrorCode::TooManyFragments);
		}
		if (reservations.size() > RHIBindingLayoutDesc::MaxSlots)
		{
			return Fail(LayoutCompositionErrorCode::InvalidReservation);
		}
		for (const auto& reservation : reservations)
		{
			if (!reservation.m_Owner.IsValid() || GetRegisterClass(reservation.m_Type) == RegisterClass::None ||
				!IsRangeValid(reservation.m_Binding, reservation.m_Count))
			{
				return Fail(LayoutCompositionErrorCode::InvalidReservation);
			}
		}
		for (size_t index = 0; index < reservations.size(); ++index)
		{
			const auto& current = reservations[index];
			const RHIBindingSlotDesc currentSlot{
				.m_Type = current.m_Type, .m_Binding = current.m_Binding,
				.m_Space = current.m_Space, .m_Count = current.m_Count };
			for (size_t otherIndex = 0; otherIndex < index; ++otherIndex)
			{
				const auto& other = reservations[otherIndex];
				const RHIBindingSlotDesc otherSlot{
					.m_Type = other.m_Type, .m_Binding = other.m_Binding,
					.m_Space = other.m_Space, .m_Count = other.m_Count };
				if ((current.m_Owner == other.m_Owner &&
					(current.m_Type != other.m_Type || current.m_Binding != other.m_Binding ||
						current.m_Space != other.m_Space || current.m_Count != other.m_Count)) ||
					(current.m_Owner != other.m_Owner && Overlaps(currentSlot, otherSlot)))
				{
					return Fail(LayoutCompositionErrorCode::InvalidReservation);
				}
			}
		}

		std::vector<Declaration> declarations;
		declarations.reserve(RHIBindingLayoutDesc::MaxSlots);
		for (const auto& fragment : fragments)
		{
			if (fragment.m_Bindings.size() > RHIBindingLayoutDesc::MaxSlots)
			{
				return Fail(LayoutCompositionErrorCode::TooManyBindings,
					OwnDeclaration({}, fragment.m_DebugName));
			}
			for (const auto& binding : fragment.m_Bindings)
			{
				auto incoming = OwnDeclaration(binding, fragment.m_DebugName);
				if (const auto error = Validate(binding); error != LayoutCompositionErrorCode::None)
				{
					return Fail(error, incoming);
				}
				auto existing = std::ranges::find(declarations, binding.m_Key,
					[](const Declaration& item) { return item.m_Binding.m_Key; });
				if (existing != declarations.end())
				{
					if (!SameDefinition(existing->m_Binding, binding))
					{
						return Fail(LayoutCompositionErrorCode::ConflictingBinding, *existing, incoming);
					}
					existing->m_Binding.m_Slot.m_Visibility |= binding.m_Slot.m_Visibility;
					if (incoming.m_DebugName < existing->m_DebugName)
					{
						existing->m_DebugName = std::move(incoming.m_DebugName);
					}
					continue;
				}
				if (declarations.size() == RHIBindingLayoutDesc::MaxSlots)
				{
					return Fail(LayoutCompositionErrorCode::TooManyBindings, incoming);
				}
				declarations.push_back(std::move(incoming));
			}
		}

		for (size_t index = 0; index < declarations.size(); ++index)
		{
			const auto& current = declarations[index];
			const auto& slot = current.m_Binding.m_Slot;
			for (size_t otherIndex = 0; otherIndex < index; ++otherIndex)
			{
				const auto& other = declarations[otherIndex];
				if (IsBindlessBindingType(slot.m_Type) || IsBindlessBindingType(other.m_Binding.m_Slot.m_Type))
				{
					if (slot.m_Type == other.m_Binding.m_Slot.m_Type)
					{
						return Fail(LayoutCompositionErrorCode::DuplicateGlobalNamespace, other, current);
					}
				}
				else if (Overlaps(slot, other.m_Binding.m_Slot))
				{
					return Fail(LayoutCompositionErrorCode::RegisterOverlap, other, current);
				}
			}
			for (const auto& reservation : reservations)
			{
				const RHIBindingSlotDesc reservedSlot{
					.m_Type = reservation.m_Type,
					.m_Binding = reservation.m_Binding,
					.m_Space = reservation.m_Space,
					.m_Count = reservation.m_Count,
				};
				const bool owner = current.m_Binding.m_Key == reservation.m_Owner;
				if ((owner && (slot.m_Type != reservation.m_Type || slot.m_Binding != reservation.m_Binding ||
					slot.m_Space != reservation.m_Space || slot.m_Count != reservation.m_Count)) ||
					(!owner && !IsBindlessBindingType(slot.m_Type) && Overlaps(slot, reservedSlot)))
				{
					LayoutBindingRequirement reserved{ .m_Key = reservation.m_Owner, .m_Slot = reservedSlot };
					return Fail(LayoutCompositionErrorCode::ReservedBindingConflict, current,
						OwnDeclaration(reserved, "Reserved binding"));
				}
			}
		}

		std::ranges::sort(declarations, [](const Declaration& lhs, const Declaration& rhs)
			{
				if (lhs.m_Binding.m_ParameterOrder != rhs.m_Binding.m_ParameterOrder)
				{
					return lhs.m_Binding.m_ParameterOrder < rhs.m_Binding.m_ParameterOrder;
				}
				return lhs.m_Binding.m_Key < rhs.m_Binding.m_Key;
			});
		ComposedLayout layout;
		layout.m_BindingCount = static_cast<uint32_t>(declarations.size());
		for (uint32_t index = 0; index < layout.m_BindingCount; ++index)
		{
			layout.m_Bindings[index] = declarations[index].m_Binding;
			layout.m_DebugNames[index] = std::move(declarations[index].m_DebugName);
			layout.m_ParameterIndices[index] = IsBindlessBindingType(layout.m_Bindings[index].m_Slot.m_Type) ?
				std::numeric_limits<uint32_t>::max() :
				layout.m_ParameterCount++;
		}
		return { .m_Layout = std::move(layout) };
	}
}
