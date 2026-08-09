#pragma once
#include "Graphics/RHI/RHITypes.h"

#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace gglab
{
	enum class RHIResourceDebugDomain : uint8_t
	{
		Unknown,
		Asset,
		RenderGraph,
		Registry,
		Transient,
		Renderer,
		Transfer,
		SwapChain,
		Diagnostics,
		DevTools,
	};

	// Exclusive bindings may be included in the native object name because the
	// previous owner is guaranteed to have completed. Aliased bindings are kept
	// in diagnostics only because old and new owners may overlap on the GPU.
	enum class RHIResourceDebugBindingMode : uint8_t
	{
		Exclusive,
		Aliased,
	};

	struct RHIResourceDebugIdentityDesc
	{
		RHIResourceDebugDomain m_Domain = RHIResourceDebugDomain::Unknown;
		std::string_view m_Category;
		std::string_view m_Label;
		std::string_view m_Source;
		std::optional<uint64_t> m_StableId;
	};

	[[nodiscard]] constexpr std::string_view RHIResourceTypeDebugText(
		RHIResourceType type) noexcept;

	[[nodiscard]] constexpr inline bool HasRHIResourceDebugIdentity(
		const RHIResourceDebugIdentityDesc& identity) noexcept
	{
		return identity.m_Domain != RHIResourceDebugDomain::Unknown ||
			!identity.m_Category.empty() || !identity.m_Label.empty() ||
			!identity.m_Source.empty() || identity.m_StableId.has_value();
	}

	[[nodiscard]] constexpr inline RHIResourceDebugIdentityDesc ResolveRHIResourceDebugIdentity(
		const RHIResourceDebugIdentityDesc& identity, std::string_view legacyName,
		RHIResourceType resourceType) noexcept
	{
		if (HasRHIResourceDebugIdentity(identity))
		{
			return identity;
		}
		return {
			.m_Domain = RHIResourceDebugDomain::Unknown,
			.m_Category = RHIResourceTypeDebugText(resourceType),
			.m_Label = legacyName.empty() ? std::string_view("Unspecified") : legacyName,
		};
	}

	struct RHIResourceDebugBindingDesc
	{
		std::string_view m_Owner;
		std::optional<uint64_t> m_Serial;
		RHIResourceDebugBindingMode m_Mode = RHIResourceDebugBindingMode::Exclusive;
	};

	struct RHIResourceDebugIdentity
	{
		RHIResourceDebugDomain m_Domain = RHIResourceDebugDomain::Unknown;
		std::string m_Category;
		std::string m_Label;
		std::string m_Source;
		std::optional<uint64_t> m_StableId;

		void Assign(const RHIResourceDebugIdentityDesc& desc)
		{
			m_Domain = desc.m_Domain;
			m_Category = desc.m_Category;
			m_Label = desc.m_Label;
			m_Source = desc.m_Source;
			m_StableId = desc.m_StableId;
		}
	};

	struct RHIResourceDebugBinding
	{
		std::string m_Owner;
		std::optional<uint64_t> m_Serial;
		RHIResourceDebugBindingMode m_Mode = RHIResourceDebugBindingMode::Exclusive;

		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return m_Owner.empty() && !m_Serial.has_value();
		}

		void Assign(const RHIResourceDebugBindingDesc& desc)
		{
			m_Owner = desc.m_Owner;
			m_Serial = desc.m_Serial;
			m_Mode = desc.m_Mode;
		}
	};

	[[nodiscard]] constexpr std::string_view RHIResourceDebugDomainText(
		RHIResourceDebugDomain domain) noexcept
	{
		switch (domain)
		{
		case RHIResourceDebugDomain::Unknown:
			return "RHI";
		case RHIResourceDebugDomain::Asset:
			return "Asset";
		case RHIResourceDebugDomain::RenderGraph:
			return "RenderGraph";
		case RHIResourceDebugDomain::Registry:
			return "Registry";
		case RHIResourceDebugDomain::Transient:
			return "Transient";
		case RHIResourceDebugDomain::Renderer:
			return "Renderer";
		case RHIResourceDebugDomain::Transfer:
			return "Transfer";
		case RHIResourceDebugDomain::SwapChain:
			return "SwapChain";
		case RHIResourceDebugDomain::Diagnostics:
			return "Diagnostics";
		case RHIResourceDebugDomain::DevTools:
			return "DevTools";
		}
		return "RHI";
	}

	[[nodiscard]] constexpr std::string_view RHIResourceTypeDebugText(RHIResourceType type) noexcept
	{
		switch (type)
		{
		case RHIResourceType::Texture:
			return "Texture";
		case RHIResourceType::Buffer:
			return "Buffer";
		default:
			return "Resource";
		}
	}

	[[nodiscard]] inline std::string FormatRHIResourceDebugName(RHIResourceType type,
		uint32_t handleIndex, uint32_t handleGeneration, const RHIResourceDebugIdentity& identity,
		const RHIResourceDebugBinding* binding = nullptr)
	{
		std::string result(RHIResourceDebugDomainText(identity.m_Domain));
		result.push_back('.');
		result.append(identity.m_Category.empty() ? RHIResourceTypeDebugText(type)
			: std::string_view(identity.m_Category));
		result.append("[");
		if (identity.m_StableId)
		{
			result.append(std::format("ID={},", *identity.m_StableId));
		}
		if (binding && binding->m_Mode == RHIResourceDebugBindingMode::Exclusive &&
			binding->m_Serial)
		{
			result.append(std::format("Bind={},", *binding->m_Serial));
		}
		result.append(std::format("RHI={}:{}]", handleIndex, handleGeneration));

		if (!identity.m_Label.empty())
		{
			result.append(" Name=");
			result.append(identity.m_Label);
		}
		if (binding && binding->m_Mode == RHIResourceDebugBindingMode::Exclusive &&
			!binding->m_Owner.empty())
		{
			result.append(" Owner=");
			result.append(binding->m_Owner);
		}
		if (!identity.m_Source.empty())
		{
			result.append(" Source=");
			result.append(identity.m_Source);
		}
		return result;
	}
}
