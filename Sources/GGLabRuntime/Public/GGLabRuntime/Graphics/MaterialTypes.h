#pragma once
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabRuntime/Core/Hash/KeyHash.h"
#include "GGLabRuntime/Core/Math/Color.h"
#include "GGLabRuntime/Core/StringId.h"
#include "GGLabRuntime/Graphics/Asset/TextureImportTypes.h"
#include "GGLabRuntime/Graphics/GraphicsHandles.h"

#include <compare>
#include <cstdint>
#include <functional>
#include <string_view>
#include <tuple>

namespace gglab
{
	enum class AlphaMode : uint32_t
	{
		Opaque,
		Mask,
		Blend,
	};

	enum class AlphaCutoffMode : uint32_t
	{
		Disabled,
		AlphaToCoverage,
		AlphaCutoff
	};

	enum class MaterialFlags : uint32_t
	{
		None = 0u,
		DoubleSided = 1u << 0,
	};
	GGLAB_ENUM_FLAGS(MaterialFlags);

	enum class MaterialDebugView : uint32_t
	{
		Lit,
		BaseColor,
		Metallic,
		Roughness,
		Normal,
	};

	enum class MaterialTextureSlot : uint32_t
	{
		BaseColor,
		MetallicRoughness,
		Normal,
		Occlusion,
		Emissive,

		Count
	};

	constexpr TextureSemantic GetMaterialTextureSlotSemantic(MaterialTextureSlot slot) noexcept
	{
		switch (slot)
		{
		case MaterialTextureSlot::BaseColor:
			return TextureSemantic::BaseColor;
		case MaterialTextureSlot::MetallicRoughness:
			return TextureSemantic::MetallicRoughness;
		case MaterialTextureSlot::Normal:
			return TextureSemantic::Normal;
		case MaterialTextureSlot::Occlusion:
			return TextureSemantic::Occlusion;
		case MaterialTextureSlot::Emissive:
			return TextureSemantic::Emissive;
		default:
			return TextureSemantic::Unknown;
		}
	}

	class RuntimeMaterialKey
	{
	public:
		RuntimeMaterialKey() noexcept = default;
		explicit RuntimeMaterialKey(std::string_view name) noexcept : m_Id(name) {}
		explicit constexpr RuntimeMaterialKey(uint64_t value) noexcept : m_Id(value) {}

		[[nodiscard]] bool IsValid() const noexcept { return m_Id.Value() != 0; }
		[[nodiscard]] uint64_t Value() const noexcept { return m_Id.Value(); }
		friend constexpr auto operator<=>(
			const RuntimeMaterialKey&, const RuntimeMaterialKey&) = default;

	private:
		StringID m_Id{};
	};

	enum class RenderMaterialDomain : uint8_t
	{
		Asset,
		Runtime,
	};

	struct RenderMaterialKey
	{
		static RenderMaterialKey FromAsset(MaterialID id) noexcept
		{
			return {
				.m_Value = static_cast<uint64_t>(id.Value()),
				.m_Domain = RenderMaterialDomain::Asset,
			};
		}

		static RenderMaterialKey FromRuntime(RuntimeMaterialKey key) noexcept
		{
			return {
				.m_Value = key.Value(),
				.m_Domain = RenderMaterialDomain::Runtime,
			};
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Domain == RenderMaterialDomain::Asset ? m_Value != MaterialID::InvalidValue
				: m_Value != 0;
		}

		[[nodiscard]] constexpr auto AsTuple() const noexcept
		{
			return std::tie(m_Domain, m_Value);
		}

		friend constexpr auto operator<=>(
			const RenderMaterialKey&, const RenderMaterialKey&) = default;

		uint64_t m_Value = MaterialID::InvalidValue;
		RenderMaterialDomain m_Domain = RenderMaterialDomain::Asset;
	};

	struct MaterialTextureBinding
	{
		TextureID m_TextureId{};
		SamplerID m_SamplerId{};
		uint32_t m_TexCoordIndex = 0;
	};

	struct MaterialProperties
	{
		MaterialTextureBinding m_BaseColorBinding{};
		MaterialTextureBinding m_EmissiveBinding{};
		MaterialTextureBinding m_MetallicRoughnessBinding{};
		MaterialTextureBinding m_NormalBinding{};
		MaterialTextureBinding m_OcclusionBinding{};

		Color m_BaseColor = Color::White;
		Color m_EmissiveColor = Color::Black;
		float m_MetallicFactor = 0.0f;
		float m_RoughnessFactor = 1.0f;
		float m_NormalScale = 1.0f;
		float m_OcclusionStrength = 1.0f;

		MaterialFlags m_Flags = MaterialFlags::None;
		AlphaMode m_AlphaMode = AlphaMode::Opaque;
		AlphaCutoffMode m_AlphaCutoffMode = AlphaCutoffMode::Disabled;
		float m_AlphaCutoff = 0.5f;
		MaterialDebugView m_DebugView = MaterialDebugView::Lit;
	};

	struct Material : MaterialProperties
	{
		MaterialID m_Id{};

		StringID m_Name{};
	};
}

namespace std
{
	template <> struct hash<gglab::RuntimeMaterialKey>
	{
		size_t operator()(gglab::RuntimeMaterialKey key) const noexcept
		{
			return std::hash<uint64_t>{}(key.Value());
		}
	};

	template <> struct hash<gglab::RenderMaterialKey>
	{
		size_t operator()(const gglab::RenderMaterialKey& key) const noexcept
		{
			return gglab::KeyHash<gglab::RenderMaterialKey>{}(key);
		}
	};
}
