#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace gglab
{
	inline constexpr std::string_view ShaderGraphPreviewDescriptorKind =
		"gglab.shader-preview-program";
	inline constexpr uint32_t ShaderGraphPreviewDescriptorVersion = 1;
	inline constexpr std::string_view ShaderGraphPreviewProgramId =
		"gglab.shader.shader-graph-preview";
	inline constexpr std::string_view ShaderGraphPreviewProgramStage = "pixel";
	inline constexpr std::string_view ShaderGraphPreviewProgramEntry = "PSMain";
	inline constexpr std::string_view ShaderGraphPreviewRuntimeBindingContractId =
		"gglab.shader-graph-preview.pass-parameters";
	inline constexpr uint32_t ShaderGraphPreviewRuntimeBindingContractVersion = 1;
	inline constexpr std::string_view ShaderGraphPreviewNumericInputContractId =
		"gglab.preview-input.surface.numeric";
	inline constexpr std::string_view ShaderGraphPreviewTexture2DInputContractId =
		"gglab.preview-input.surface.texture2d";

	enum class ShaderGraphPreviewViewMode : uint32_t
	{
		Combined = 0,
		BaseColor = 1,
		Emissive = 2,
		Metallic = 3,
		Roughness = 4,
		Opacity = 5,
	};

	struct ShaderGraphPreviewViewModeProjection final
	{
		std::string_view m_Name;
		std::string_view m_HlslSymbol;
		ShaderGraphPreviewViewMode m_Value = ShaderGraphPreviewViewMode::Combined;
	};

	inline constexpr std::array ShaderGraphPreviewViewModes{
		ShaderGraphPreviewViewModeProjection{
			"combined", "ShaderGraphPreviewViewModeCombined",
			ShaderGraphPreviewViewMode::Combined },
		ShaderGraphPreviewViewModeProjection{
			"base-color", "ShaderGraphPreviewViewModeBaseColor",
			ShaderGraphPreviewViewMode::BaseColor },
		ShaderGraphPreviewViewModeProjection{
			"emissive", "ShaderGraphPreviewViewModeEmissive",
			ShaderGraphPreviewViewMode::Emissive },
		ShaderGraphPreviewViewModeProjection{
			"metallic", "ShaderGraphPreviewViewModeMetallic",
			ShaderGraphPreviewViewMode::Metallic },
		ShaderGraphPreviewViewModeProjection{
			"roughness", "ShaderGraphPreviewViewModeRoughness",
			ShaderGraphPreviewViewMode::Roughness },
		ShaderGraphPreviewViewModeProjection{
			"opacity", "ShaderGraphPreviewViewModeOpacity",
			ShaderGraphPreviewViewMode::Opacity },
	};

	struct alignas(16) ShaderGraphPreviewPassParameters final
	{
		uint32_t ViewIndex = 0;
		uint32_t ViewMode = static_cast<uint32_t>(ShaderGraphPreviewViewMode::Combined);
		float Metal = 0.0f;
		float Roughness = 0.5f;
		std::array<float, 3> Tint{ 0.15f, 0.55f, 1.0f };
		uint32_t TextureIndex = 0;
		uint32_t SamplerIndex = 0;
		std::array<uint32_t, 3> Padding{};
	};

	struct ShaderGraphPreviewPassAbiMember final
	{
		std::string_view m_Name;
		std::string_view m_HlslType;
		size_t m_Offset = 0;
	};

	inline constexpr std::array ShaderGraphPreviewPassAbiMembers{
		ShaderGraphPreviewPassAbiMember{
			"ViewIndex", "uint", offsetof(ShaderGraphPreviewPassParameters, ViewIndex) },
		ShaderGraphPreviewPassAbiMember{
			"ViewMode", "uint", offsetof(ShaderGraphPreviewPassParameters, ViewMode) },
		ShaderGraphPreviewPassAbiMember{
			"Metal", "float", offsetof(ShaderGraphPreviewPassParameters, Metal) },
		ShaderGraphPreviewPassAbiMember{
			"Roughness", "float", offsetof(ShaderGraphPreviewPassParameters, Roughness) },
		ShaderGraphPreviewPassAbiMember{
			"Tint", "float3", offsetof(ShaderGraphPreviewPassParameters, Tint) },
		ShaderGraphPreviewPassAbiMember{
			"TextureIndex", "uint", offsetof(ShaderGraphPreviewPassParameters, TextureIndex) },
		ShaderGraphPreviewPassAbiMember{
			"SamplerIndex", "uint", offsetof(ShaderGraphPreviewPassParameters, SamplerIndex) },
		ShaderGraphPreviewPassAbiMember{
			"Padding", "uint3", offsetof(ShaderGraphPreviewPassParameters, Padding) },
	};

	static_assert(std::is_standard_layout_v<ShaderGraphPreviewPassParameters>);
	static_assert(std::is_trivially_copyable_v<ShaderGraphPreviewPassParameters>);
	static_assert(alignof(ShaderGraphPreviewPassParameters) == 16);
	static_assert(offsetof(ShaderGraphPreviewPassParameters, ViewIndex) == 0);
	static_assert(offsetof(ShaderGraphPreviewPassParameters, ViewMode) == 4);
	static_assert(offsetof(ShaderGraphPreviewPassParameters, Metal) == 8);
	static_assert(offsetof(ShaderGraphPreviewPassParameters, Roughness) == 12);
	static_assert(offsetof(ShaderGraphPreviewPassParameters, Tint) == 16);
	static_assert(offsetof(ShaderGraphPreviewPassParameters, TextureIndex) == 28);
	static_assert(offsetof(ShaderGraphPreviewPassParameters, SamplerIndex) == 32);
	static_assert(offsetof(ShaderGraphPreviewPassParameters, Padding) == 36);
	static_assert(sizeof(ShaderGraphPreviewPassParameters) == 48);
}
