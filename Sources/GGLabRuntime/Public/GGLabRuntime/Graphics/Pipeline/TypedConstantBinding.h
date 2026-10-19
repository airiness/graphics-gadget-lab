#pragma once
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"

#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>

namespace gglab
{
	template <typename T>
	struct ConstantPayloadTraits;

	template <typename T>
	concept ConstantPayload = std::is_trivially_copyable_v<T> &&
		std::is_standard_layout_v<T> && sizeof(T) % sizeof(uint32_t) == 0 &&
		requires { { ConstantPayloadTraits<T>::Tag } -> std::convertible_to<LayoutPayloadTag>; };

	// Borrows one frame's immutable upload storage; copying does not extend GPU lifetime.
	template <ConstantPayload T>
	class TypedConstantSnapshot final
	{
	public:
		[[nodiscard]] static TypedConstantSnapshot Upload(
			const RHIFrameUploads& uploads, const T& value) noexcept
		{
			TypedConstantSnapshot result;
			result.m_Binding = uploads.UploadConstants(value);
			return result;
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Binding.IsValid() && m_Binding.m_SizeInBytes == sizeof(T) &&
				m_Binding.m_UploadBatchId != 0;
		}
		// Compatibility view for consumers that still use the numeric RHI API.
		[[nodiscard]] const RHIConstantBufferBinding& GetBinding() const noexcept { return m_Binding; }

	private:
		RHIConstantBufferBinding m_Binding{};
	};

	// Copies/moves of a resolved layout share its immutable logical instance. A separately
	// resolved value has its own reference scope, even if its native handle is deduplicated.
	template <typename T>
	class ResolvedBindingRef final
	{
		static_assert(ConstantPayload<T>);

	public:
		[[nodiscard]] static ResolvedBindingRef Resolve(
			const ResolvedRenderLayout& layout, LayoutBindingKey key) noexcept
		{
			ResolvedBindingRef result;
			if (const auto* contract = layout.GetContract())
			{
				for (const auto& binding : contract->GetBindings())
				{
					if (binding.m_Key == key && binding.m_PayloadTag == ConstantPayloadTraits<T>::Tag &&
						binding.m_Slot.m_Type == RHIBindingType::ConstantBuffer &&
						binding.m_Slot.m_Count == 1 &&
						(binding.m_Slot.m_SizeInBytes == 0 || binding.m_Slot.m_SizeInBytes == sizeof(T)))
					{
						if (const auto parameter = contract->FindParameterIndex(key))
						{
							result.m_Contract = layout.m_Contract;
							result.m_ParameterIndex = *parameter;
						}
						break;
					}
				}
			}
			return result;
		}

		[[nodiscard]] bool IsValid() const noexcept { return !m_Contract.expired(); }
		[[nodiscard]] std::optional<uint32_t> GetParameterIndex(
			const ResolvedRenderLayout& layout) const noexcept
		{
			const auto contract = m_Contract.lock();
			return contract && contract == layout.m_Contract ?
				std::optional<uint32_t>{ m_ParameterIndex } :
				std::nullopt;
		}

	private:
		std::weak_ptr<const ComposedLayout> m_Contract;
		uint32_t m_ParameterIndex = 0;
	};

	// Logical validation happens here; the encoder remains authoritative for batch ownership,
	// upload-storage policy, resource usage and native range/alignment validation.
	template <typename Context, ConstantPayload T>
		requires (std::derived_from<Context, RHIGraphicsCommandContext> ||
			std::derived_from<Context, RHIComputeCommandContext>)
	[[nodiscard]] bool BindConstantSnapshot(Context& context, const ResolvedRenderLayout& layout,
		const ResolvedBindingRef<T>& reference, const TypedConstantSnapshot<T>& snapshot) noexcept
	{
		const auto parameter = reference.GetParameterIndex(layout);
		if (!parameter || !snapshot.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Typed constant binding rejected for payload {}: incompatible logical reference or unavailable snapshot.",
				ConstantPayloadTraits<T>::Tag.Value());
			context.RejectRecording();
			return false;
		}
		return context.SetConstantBuffer(*parameter, snapshot.GetBinding());
	}
}
