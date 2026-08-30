#include "ShaderGraphPreviewProgramContractSelfTests.h"

#include "DevelopmentShaderPaths.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "ShaderArtifactRuntime/ShaderGraphPreviewProgram.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace gglab
{
	namespace
	{
		using Json = nlohmann::json;

		struct InputExpectation final
		{
			std::string_view m_Source;
			std::string_view m_StableId;
			std::string_view m_ParameterClass;
			std::string_view m_GraphValueType;
			std::string_view m_GeneratedType;
		};

		[[nodiscard]] std::optional<Json> ReadJsonObject(
			const std::filesystem::path& path) noexcept
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return std::nullopt;
			}
			Json document = Json::parse(input, nullptr, /*allow_exceptions=*/false);
			return input && document.is_object()
				? std::optional<Json>(std::move(document))
				: std::nullopt;
		}

		[[nodiscard]] std::optional<std::string> ReadText(
			const std::filesystem::path& path) noexcept
		{
			std::ifstream input(path, std::ios::binary | std::ios::ate);
			if (!input)
			{
				return std::nullopt;
			}
			const std::streampos end = input.tellg();
			if (end < 0)
			{
				return std::nullopt;
			}
			std::string text(static_cast<size_t>(end), '\0');
			input.seekg(0, std::ios::beg);
			input.read(text.data(), static_cast<std::streamsize>(text.size()));
			return input || text.empty() ? std::optional<std::string>(std::move(text))
								 : std::nullopt;
		}

		[[nodiscard]] const Json* FindField(
			const Json& object, std::string_view name) noexcept
		{
			if (!object.is_object())
			{
				return nullptr;
			}
			const auto iterator = object.find(std::string(name));
			return iterator != object.end() ? &*iterator : nullptr;
		}

		[[nodiscard]] bool StringFieldEquals(const Json& object,
			std::string_view name, std::string_view expected) noexcept
		{
			const Json* field = FindField(object, name);
			return field && field->is_string() &&
				field->get_ref<const Json::string_t&>() == expected;
		}

		[[nodiscard]] bool IntegerFieldEquals(
			const Json& object, std::string_view name, uint32_t expected) noexcept
		{
			const Json* field = FindField(object, name);
			return field && field->is_number_integer() && *field == expected;
		}

		[[nodiscard]] bool NumberFieldEquals(
			const Json& object, std::string_view name, float expected) noexcept
		{
			const Json* field = FindField(object, name);
			return field && field->is_number() &&
				std::abs(field->get<double>() - static_cast<double>(expected)) <= 0.000001;
		}

		[[nodiscard]] bool NumberEquals(const Json& value, float expected) noexcept
		{
			return value.is_number() &&
				std::abs(value.get<double>() - static_cast<double>(expected)) <= 0.000001;
		}

		[[nodiscard]] const Json* FindObjectById(
			const Json& array, std::string_view id) noexcept
		{
			if (!array.is_array())
			{
				return nullptr;
			}
			for (const Json& value : array)
			{
				if (StringFieldEquals(value, "id", id))
				{
					return &value;
				}
			}
			return nullptr;
		}

		[[nodiscard]] bool ValidateProgramRef(
			const Json& value, const ShaderProgramRef& expected) noexcept
		{
			return StringFieldEquals(value, "programId", expected.m_ProgramId) &&
				StringFieldEquals(value, "variant", expected.m_VariantId) &&
				StringFieldEquals(value, "stage", ShaderGraphPreviewProgramStage);
		}

		template <size_t InputCount>
		[[nodiscard]] bool ValidateInputContract(const Json& contract,
			uint32_t profileVersion, const ShaderProgramRef& programRef,
			const std::array<InputExpectation, InputCount>& expectedInputs) noexcept
		{
			const Json* profile = FindField(contract, "profile");
			const Json* serializedProgramRef = FindField(contract, "programRef");
			const Json* generatedFunction = FindField(contract, "generatedFunction");
			const Json* orderedInputs = FindField(contract, "orderedInputs");
			if (!profile || !serializedProgramRef || !generatedFunction || !orderedInputs ||
				!orderedInputs->is_array() || orderedInputs->size() != expectedInputs.size() ||
				!StringFieldEquals(*profile, "id", "gglab.surface") ||
				!IntegerFieldEquals(*profile, "profileVersion", profileVersion) ||
				!IntegerFieldEquals(*profile, "descriptorVersion", profileVersion) ||
				!ValidateProgramRef(*serializedProgramRef, programRef) ||
				!StringFieldEquals(*generatedFunction, "name", "EvaluateSurface") ||
				!StringFieldEquals(*generatedFunction, "stage", "pixel"))
			{
				return false;
			}

			for (size_t index = 0; index < expectedInputs.size(); ++index)
			{
				const InputExpectation& expected = expectedInputs[index];
				const Json& input = (*orderedInputs)[index];
				if (!StringFieldEquals(input, "source", expected.m_Source) ||
					!StringFieldEquals(input, "stableId", expected.m_StableId) ||
					!StringFieldEquals(input, "graphValueType", expected.m_GraphValueType) ||
					!StringFieldEquals(input, "generatedType", expected.m_GeneratedType))
				{
					return false;
				}
				const Json* parameterClass = FindField(input, "parameterClass");
				if (expected.m_ParameterClass.empty()
					? parameterClass != nullptr
					: !parameterClass || !parameterClass->is_string() ||
						parameterClass->get_ref<const Json::string_t&>() !=
							expected.m_ParameterClass)
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool ValidateDescriptorViewModes(const Json& descriptor) noexcept
		{
			const Json* viewModes = FindField(descriptor, "viewModes");
			if (!viewModes || !viewModes->is_array() ||
				viewModes->size() != ShaderGraphPreviewViewModes.size())
			{
				return false;
			}
			for (size_t index = 0; index < ShaderGraphPreviewViewModes.size(); ++index)
			{
				const ShaderGraphPreviewViewModeProjection& projection =
					ShaderGraphPreviewViewModes[index];
				const Json& value = (*viewModes)[index];
				if (!StringFieldEquals(value, "name", projection.m_Name) ||
					!StringFieldEquals(value, "symbol", projection.m_HlslSymbol) ||
					!IntegerFieldEquals(value, "value", static_cast<uint32_t>(projection.m_Value)))
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool ValidateHlslProjection(std::string_view hlsl) noexcept
		{
			for (const ShaderGraphPreviewViewModeProjection& mode :
				ShaderGraphPreviewViewModes)
			{
				const std::string declaration = std::format("static const uint {} = {};",
					mode.m_HlslSymbol, static_cast<uint32_t>(mode.m_Value));
				if (hlsl.find(declaration) == std::string_view::npos)
				{
					return false;
				}
			}

			size_t fieldPosition = hlsl.find("struct ShaderGraphPreviewPassParameters");
			for (const ShaderGraphPreviewPassAbiMember& member :
				ShaderGraphPreviewPassAbiMembers)
			{
				const std::string declaration =
					std::format("{} {};", member.m_HlslType, member.m_Name);
				fieldPosition = hlsl.find(declaration, fieldPosition);
				if (fieldPosition == std::string_view::npos)
				{
					return false;
				}
				fieldPosition += declaration.size();
			}
			return hlsl.find(
				"ConstantBuffer<ShaderGraphPreviewPassParameters> g_Preview : register(b2);") !=
				std::string_view::npos;
		}
	}

	void RunShaderGraphPreviewProgramContractSelfTests(SelfTestContext& context) noexcept
	{
		const std::filesystem::path shaderRoot =
			ResolveShaderSourceRoot(win32::GetExecutableDirectory());
		const std::filesystem::path contractRoot =
			shaderRoot / L"Programs" / L"ShaderGraphPreview";
		const std::optional<Json> descriptor = ReadJsonObject(contractRoot / L"descriptor.json");
		const std::optional<std::string> hlsl =
			ReadText(contractRoot / L"ShaderGraphPreviewProgram.hlsli");

		const Json* program = descriptor ? FindField(*descriptor, "program") : nullptr;
		const Json* runtimeBinding =
			descriptor ? FindField(*descriptor, "runtimeBindingContract") : nullptr;
		context.Check(descriptor &&
			StringFieldEquals(*descriptor, "kind", ShaderGraphPreviewDescriptorKind) &&
			IntegerFieldEquals(
				*descriptor, "descriptorVersion", ShaderGraphPreviewDescriptorVersion) &&
			program && StringFieldEquals(*program, "id", ShaderGraphPreviewProgramId) &&
			StringFieldEquals(*program, "stage", ShaderGraphPreviewProgramStage) &&
			StringFieldEquals(*program, "entry", ShaderGraphPreviewProgramEntry) &&
			runtimeBinding && StringFieldEquals(
				*runtimeBinding, "id", ShaderGraphPreviewRuntimeBindingContractId) &&
			IntegerFieldEquals(*runtimeBinding, "version",
				ShaderGraphPreviewRuntimeBindingContractVersion),
			"Preview Program descriptor publishes the frozen program and binding identities");

		context.Check(descriptor && ValidateDescriptorViewModes(*descriptor),
			"Preview Program descriptor is the authority for every C++ view-mode value");
		context.Check(hlsl && ValidateHlslProjection(*hlsl),
			"Preview HLSL view modes and pass fields project the shared C++ contract");

		constexpr std::array numericInputs{
			InputExpectation{ "graphParameter", "p.metal", "ScalarParameter", "float", "float" },
			InputExpectation{ "graphParameter", "p.tint", "VectorParameter", "float3", "float3" },
			InputExpectation{ "graphVisibleInput", "uv0", "", "float2", "float2" },
		};
		constexpr std::array textureInputs{
			InputExpectation{ "graphParameter", "p.rough", "ScalarParameter", "float", "float" },
			InputExpectation{
				"graphParameter", "p.tex", "Texture2DParameter", "Texture2D", "uint2" },
			InputExpectation{ "graphVisibleInput", "uv0", "", "float2", "float2" },
		};
		const Json* inputContracts = descriptor ? FindField(*descriptor, "inputContracts") : nullptr;
		const Json* numeric = inputContracts
			? FindObjectById(*inputContracts, ShaderGraphPreviewNumericInputContractId)
			: nullptr;
		const Json* texture = inputContracts
			? FindObjectById(*inputContracts, ShaderGraphPreviewTexture2DInputContractId)
			: nullptr;
		context.Check(numeric && ValidateInputContract(
			*numeric, 1, shader_programs::ShaderGraphPreviewSurfaceV1Pixel, numericInputs),
			"Preview descriptor freezes the numeric-v1 input order and ProgramRef");
		context.Check(texture && ValidateInputContract(
			*texture, 2, shader_programs::ShaderGraphPreviewSurfaceV2Pixel, textureInputs),
			"Preview descriptor freezes the texture2d-v2 binding-pair order and ProgramRef");

		const Json* numericValues = numeric ? FindField(*numeric, "labValues") : nullptr;
		const Json* textureValues = texture ? FindField(*texture, "labValues") : nullptr;
		const Json* metal = numericValues ? FindField(*numericValues, "metal") : nullptr;
		const Json* tint = numericValues ? FindField(*numericValues, "tint") : nullptr;
		const Json* roughness = textureValues ? FindField(*textureValues, "roughness") : nullptr;
		const Json* tintDefault = tint ? FindField(*tint, "default") : nullptr;
		const ShaderGraphPreviewPassParameters defaults{};
		context.Check(metal && roughness && tintDefault && tintDefault->is_array() &&
			tintDefault->size() == defaults.Tint.size() &&
			NumberFieldEquals(*metal, "default", defaults.Metal) &&
			NumberFieldEquals(*roughness, "default", defaults.Roughness) &&
			NumberEquals((*tintDefault)[0], defaults.Tint[0]) &&
			NumberEquals((*tintDefault)[1], defaults.Tint[1]) &&
			NumberEquals((*tintDefault)[2], defaults.Tint[2]) &&
			defaults.Padding == std::array<uint32_t, 3>{},
			"Preview descriptor defaults project to the zero-padded 48-byte CPU upload value");
	}
}
