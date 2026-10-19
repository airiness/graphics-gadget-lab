#pragma once
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"

#include <array>
#include <cstdint>
#include <optional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	class RHIPipelineSystem;
	template <typename T>
	class ResolvedBindingRef;
	// Process-local domain keys, independent of shader registers and native parameters.
	struct LayoutBindingKeyTag;
	using LayoutBindingKey = TypedIndex<LayoutBindingKeyTag>;
	struct LayoutPayloadIdentityTag;
	using LayoutPayloadTag = TypedIndex<LayoutPayloadIdentityTag>;

	struct LayoutBindingRequirement
	{
		LayoutBindingKey m_Key{};
		RHIBindingSlotDesc m_Slot{};
		LayoutPayloadTag m_PayloadTag{};
		// Family-defined ordering; ties use the semantic key, never fragment enumeration.
		uint32_t m_ParameterOrder = 0;
	};

	// Borrowed only during composition. A composed value's bindings can form another fragment.
	struct LayoutFragment
	{
		std::string_view m_DebugName{};
		std::span<const LayoutBindingRequirement> m_Bindings{};
	};

	struct LayoutBindingReservation
	{
		LayoutBindingKey m_Owner{};
		RHIBindingType m_Type = RHIBindingType::Unknown;
		uint32_t m_Binding = 0;
		uint32_t m_Space = 0;
		uint32_t m_Count = 1;
	};

	enum class LayoutCompositionErrorCode : uint8_t
	{
		None,
		TooManyFragments,
		TooManyBindings,
		InvalidKey,
		InvalidType,
		InvalidVisibility,
		InvalidCount,
		RegisterRangeOverflow,
		InvalidSize,
		InvalidBufferMode,
		InvalidPayloadType,
		ConflictingBinding,
		RegisterOverlap,
		DuplicateGlobalNamespace,
		InvalidReservation,
		ReservedBindingConflict,
	};

	struct LayoutDeclarationSite
	{
		std::string m_FragmentName;
		// Diagnostic declarations never retain the input's borrowed debug-name pointer.
		LayoutBindingRequirement m_Declaration{};
	};

	struct LayoutCompositionError
	{
		LayoutCompositionErrorCode m_Code = LayoutCompositionErrorCode::None;
		LayoutDeclarationSite m_First;
		LayoutDeclarationSite m_Second;
	};

	struct LayoutCompositionResult;

	class ComposedLayout final
	{
	public:
		[[nodiscard]] std::span<const LayoutBindingRequirement> GetBindings() const noexcept;
		[[nodiscard]] uint32_t GetParameterCount() const noexcept { return m_ParameterCount; }
		[[nodiscard]] std::optional<uint32_t> FindParameterIndex(LayoutBindingKey key) const noexcept;
		// Debug names borrow this value; consume the descriptor while this value is alive.
		[[nodiscard]] RHIBindingLayoutDesc BuildRHIDesc(const char* debugName = nullptr) const noexcept;
		[[nodiscard]] bool operator==(const ComposedLayout& other) const noexcept;

	private:
		ComposedLayout() = default;
		friend LayoutCompositionResult ComposeLayoutFragments(std::span<const LayoutFragment>,
			std::span<const LayoutBindingReservation>) noexcept;

		std::array<LayoutBindingRequirement, RHIBindingLayoutDesc::MaxSlots> m_Bindings{};
		std::array<std::string, RHIBindingLayoutDesc::MaxSlots> m_DebugNames{};
		std::array<uint32_t, RHIBindingLayoutDesc::MaxSlots> m_ParameterIndices{};
		uint32_t m_BindingCount = 0;
		uint32_t m_ParameterCount = 0;
	};

	struct LayoutCompositionResult
	{
		std::optional<ComposedLayout> m_Layout;
		LayoutCompositionError m_Error;

		[[nodiscard]] bool IsSuccess() const noexcept { return m_Layout.has_value(); }
	};

	// Owns logical metadata independently of backend native-layout deduplication.
	// The backend retains ownership of the native handle's lifetime.
	class ResolvedRenderLayout final
	{
	public:
		[[nodiscard]] static ResolvedRenderLayout Create(RHIPipelineSystem& pipelineSystem,
			const ComposedLayout& contract, const char* debugName = nullptr) noexcept;
		[[nodiscard]] bool IsValid() const noexcept { return m_Contract != nullptr; }
		[[nodiscard]] const ComposedLayout* GetContract() const noexcept;
		[[nodiscard]] RHIBindingLayoutHandle GetNativeLayout() const noexcept
		{
			return IsValid() ? m_NativeLayout : RHIBindingLayoutHandle{};
		}

	private:
		template <typename T>
		friend class ResolvedBindingRef;
		std::shared_ptr<const ComposedLayout> m_Contract;
		RHIBindingLayoutHandle m_NativeLayout{};
	};

	inline constexpr uint32_t MaxLayoutFragments = 64;

	// Failure returns no partial layout. Reservations are optional domain policy, not bindings.
	[[nodiscard]] LayoutCompositionResult ComposeLayoutFragments(
		std::span<const LayoutFragment> fragments,
		std::span<const LayoutBindingReservation> reservations = {}) noexcept;
}
