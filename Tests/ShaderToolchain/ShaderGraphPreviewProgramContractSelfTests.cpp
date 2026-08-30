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

		struct ParameterExpectation final
		{
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

		[[nodiscard]] bool StringFieldsEqual(const Json& lhs,
			std::string_view lhsName, const Json& rhs, std::string_view rhsName) noexcept
		{
			const Json* lhsField = FindField(lhs, lhsName);
			const Json* rhsField = FindField(rhs, rhsName);
			return lhsField && rhsField && lhsField->is_string() && rhsField->is_string() &&
				*lhsField == *rhsField;
		}

		[[nodiscard]] bool IntegerFieldsEqual(const Json& lhs,
			std::string_view lhsName, const Json& rhs, std::string_view rhsName) noexcept
		{
			const Json* lhsField = FindField(lhs, lhsName);
			const Json* rhsField = FindField(rhs, rhsName);
			return lhsField && rhsField && lhsField->is_number_integer() &&
				rhsField->is_number_integer() && *lhsField == *rhsField;
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

		template <size_t ParameterCount>
		[[nodiscard]] bool ValidateInputContract(const Json& contract,
			const Json& profileDescriptor, const ShaderProgramRef& programRef,
			const std::array<ParameterExpectation, ParameterCount>& expectedParameters) noexcept
		{
			const Json* profile = FindField(contract, "profile");
			const Json* serializedProgramRef = FindField(contract, "programRef");
			const Json* generatedFunction = FindField(contract, "generatedFunction");
			const Json* orderedInputs = FindField(contract, "orderedInputs");
			const Json* profileGeneratedFunction =
				FindField(profileDescriptor, "generatedFunction");
			const Json* graphVisibleInputs =
				FindField(profileDescriptor, "graphVisibleInputs");
			if (!profile || !serializedProgramRef || !generatedFunction || !orderedInputs ||
				!profileGeneratedFunction || !graphVisibleInputs || !orderedInputs->is_array() ||
				!graphVisibleInputs->is_array() ||
				orderedInputs->size() != expectedParameters.size() + graphVisibleInputs->size() ||
				!StringFieldsEqual(*profile, "id", profileDescriptor, "profileId") ||
				!IntegerFieldsEqual(
					*profile, "profileVersion", profileDescriptor, "profileVersion") ||
				!IntegerFieldsEqual(
					*profile, "descriptorVersion", profileDescriptor, "descriptorVersion") ||
				!ValidateProgramRef(*serializedProgramRef, programRef) ||
				!StringFieldsEqual(
					*generatedFunction, "name", *profileGeneratedFunction, "name") ||
				!StringFieldsEqual(
					*generatedFunction, "stage", *profileGeneratedFunction, "stage"))
			{
				return false;
			}

			for (size_t index = 0; index < expectedParameters.size(); ++index)
			{
				const ParameterExpectation& expected = expectedParameters[index];
				const Json& input = (*orderedInputs)[index];
				if (!StringFieldEquals(input, "source", "graphParameter") ||
					!StringFieldEquals(input, "stableId", expected.m_StableId) ||
					!StringFieldEquals(input, "graphValueType", expected.m_GraphValueType) ||
					!StringFieldEquals(input, "generatedType", expected.m_GeneratedType))
				{
					return false;
				}
				if (!StringFieldEquals(input, "parameterClass", expected.m_ParameterClass))
				{
					return false;
				}
			}

			for (size_t index = 0; index < graphVisibleInputs->size(); ++index)
			{
				const Json& input = (*orderedInputs)[expectedParameters.size() + index];
				const Json& profileInput = (*graphVisibleInputs)[index];
				if (!StringFieldEquals(input, "source", "graphVisibleInput") ||
					FindField(input, "parameterClass") != nullptr ||
					!StringFieldsEqual(input, "stableId", profileInput, "id") ||
					!StringFieldsEqual(input, "graphValueType", profileInput, "type") ||
					!StringFieldsEqual(input, "generatedType", profileInput, "type"))
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool ValidateTextureBindingSignature(const Json& contract,
			const Json& profileDescriptor) noexcept
		{
			const Json* orderedInputs = FindField(contract, "orderedInputs");
			const Json* samplingContract = FindField(profileDescriptor, "samplingContract");
			const Json* generatedTextureSignature = samplingContract
				? FindField(*samplingContract, "generatedTextureSignature")
				: nullptr;
			const Json* componentOrder = generatedTextureSignature
				? FindField(*generatedTextureSignature, "componentOrder")
				: nullptr;
			if (!orderedInputs || !orderedInputs->is_array() || orderedInputs->size() < 2 ||
				!generatedTextureSignature || !componentOrder || !componentOrder->is_array() ||
				componentOrder->size() != 2)
			{
				return false;
			}

			const Json& textureInput = (*orderedInputs)[1];
			const Json* bindingPair = FindField(textureInput, "bindingPair");
			const Json* parameterType = FindField(*generatedTextureSignature, "parameterType");
			if (!bindingPair || !bindingPair->is_array() || bindingPair->size() != 2 ||
				!parameterType || !parameterType->is_string() ||
				!StringFieldEquals(textureInput, "generatedType",
					parameterType->get_ref<const Json::string_t&>()))
			{
				return false;
			}

			const Json& textureComponent = (*componentOrder)[0];
			const Json& samplerComponent = (*componentOrder)[1];
			return IntegerFieldEquals(textureComponent, "position", 0) &&
				StringFieldEquals(textureComponent, "role", "textureBindingIndex") &&
				IntegerFieldEquals(samplerComponent, "position", 1) &&
				StringFieldEquals(samplerComponent, "role", "samplerBindingIndex") &&
				(*bindingPair)[0].is_string() && (*bindingPair)[1].is_string() &&
				(*bindingPair)[0] == *FindField(textureComponent, "role") &&
				(*bindingPair)[1] == *FindField(samplerComponent, "role");
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
		const std::filesystem::path surfaceProfileRoot =
			shaderRoot / L"Profiles" / L"GGLab.Surface";
		const std::optional<Json> surfaceProfileV1 =
			ReadJsonObject(surfaceProfileRoot / L"1" / L"descriptor.json");
		const std::optional<Json> surfaceProfileV2 =
			ReadJsonObject(surfaceProfileRoot / L"2" / L"descriptor.json");
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

		constexpr std::array numericParameters{
			ParameterExpectation{ "p.metal", "ScalarParameter", "float", "float" },
			ParameterExpectation{ "p.tint", "VectorParameter", "float3", "float3" },
		};
		constexpr std::array textureParameters{
			ParameterExpectation{ "p.rough", "ScalarParameter", "float", "float" },
			ParameterExpectation{ "p.tex", "Texture2DParameter", "Texture2D", "uint2" },
		};
		const Json* inputContracts = descriptor ? FindField(*descriptor, "inputContracts") : nullptr;
		const Json* numeric = inputContracts
			? FindObjectById(*inputContracts, ShaderGraphPreviewNumericInputContractId)
			: nullptr;
		const Json* texture = inputContracts
			? FindObjectById(*inputContracts, ShaderGraphPreviewTexture2DInputContractId)
			: nullptr;
		context.Check(numeric && surfaceProfileV1 && ValidateInputContract(*numeric,
			*surfaceProfileV1, shader_programs::ShaderGraphPreviewSurfaceV1Pixel,
			numericParameters),
			"Preview numeric-v1 contract projects the real Surface Profile descriptor");
		context.Check(texture && surfaceProfileV2 && ValidateInputContract(*texture,
			*surfaceProfileV2, shader_programs::ShaderGraphPreviewSurfaceV2Pixel,
			textureParameters),
			"Preview texture2d-v2 contract projects the real Surface Profile descriptor");
		context.Check(texture && surfaceProfileV2 &&
			ValidateTextureBindingSignature(*texture, *surfaceProfileV2),
			"Preview texture2d-v2 bindingPair strictly projects texture then sampler binding roles");

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
