#include "Artifact/ShaderArtifactManifestIO.h"
#include "Artifact/PublicationTransaction.h"
#include "Testing/ShaderArtifactManifestIOTestAccess.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "Targets/ShaderTargetWireNames.h"
#include "Wire/ShaderWireNames.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] constexpr int HexDigitValue(char value) noexcept
		{
			if (value >= '0' && value <= '9')
			{
				return value - '0';
			}
			if (value >= 'a' && value <= 'f')
			{
				return value - 'a' + 10;
			}
			if (value >= 'A' && value <= 'F')
			{
				return value - 'A' + 10;
			}
			return -1;
		}

		[[nodiscard]] std::optional<Sha256Digest> ParseHexSha256Digest(
			std::string_view hex) noexcept
		{
			if (hex.size() != Sha256Digest::Size * 2)
			{
				return std::nullopt;
			}
			Sha256Digest digest{};
			for (std::size_t byteIndex = 0; byteIndex < Sha256Digest::Size; ++byteIndex)
			{
				const int high = HexDigitValue(hex[byteIndex * 2]);
				const int low = HexDigitValue(hex[byteIndex * 2 + 1]);
				if (high < 0 || low < 0)
				{
					return std::nullopt;
				}
				digest.m_Value[byteIndex] =
					static_cast<std::byte>((high << 4) | low);
			}
			return digest;
		}

		[[nodiscard]] std::optional<ShaderBinary> ReadFileBinary(
			const std::filesystem::path& path) noexcept
		{
			std::error_code errorCode;
			const auto fileSize = std::filesystem::file_size(path, errorCode);
			if (errorCode)
			{
				return std::nullopt;
			}
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return std::nullopt;
			}
			ShaderBinary binary(static_cast<size_t>(fileSize));
			input.read(static_cast<char*>(binary.Data()), static_cast<std::streamsize>(fileSize));
			if (!input || input.gcount() != static_cast<std::streamsize>(fileSize))
			{
				return std::nullopt;
			}
			return binary;
		}
	}

	namespace
	{
		// Null selects the production read-once implementation.
		testing::BinaryReadOnceOverride g_BinaryReadOnceOverride = nullptr;
		// Null disables scripted rename failures.
		testing::PublishFileFailureInjector g_PublishFileFailureInjector = nullptr;
	}

	namespace testing
	{
		std::optional<BinaryReadWithDigest> ReadBinaryWithDigestOnce(
			const std::filesystem::path& path) noexcept
		{
			std::optional<ShaderBinary> binary = ReadFileBinary(path);
			if (!binary.has_value())
			{
				return std::nullopt;
			}
			BinaryReadWithDigest result{};
			result.m_Digest = ComputeSha256(std::span(
				static_cast<const std::byte*>(binary->Data()), binary->SizeInBytes()));
			result.m_Binary = std::move(*binary);
			return result;
		}

		void OverrideBinaryReadOnce(BinaryReadOnceOverride overrideFn) noexcept
		{
			g_BinaryReadOnceOverride = overrideFn;
		}

		void OverridePublishFileFailure(PublishFileFailureInjector injector) noexcept
		{
			g_PublishFileFailureInjector = injector;
		}
	}

	namespace
	{
		[[nodiscard]] nlohmann::json MakeManifestDocument(
			const ShaderArtifactManifest& manifest)
		{
			nlohmann::json document;
			document["schemaVersion"] = static_cast<std::int64_t>(manifest.m_SchemaVersion);
			document["recipeHashSchema"] = static_cast<std::int64_t>(manifest.m_RecipeHashSchema);
			document["recipeId"] = Sha256DigestToHex(manifest.m_RecipeId.m_DurableDigest);
			document["buildKey"] = Sha256DigestToHex(manifest.m_BuildKey.m_DurableDigest);
			nlohmann::json compiler;
			compiler["kind"] = "dxc";
			compiler["identity"] = utils::ToString(manifest.m_CompilerIdentity.m_CanonicalIdentity);
			document["compiler"] = std::move(compiler);
			document["targetProfile"] = ShaderTargetWire::Name(manifest.m_TargetProfile);
			document["binaryFormat"] = ShaderBinaryFormatWire::Name(manifest.m_BinaryFormat);
			document["spirvTargetEnvironment"] =
				ShaderSpirVTargetEnvironmentWire::Name(manifest.m_SpirVTargetEnvironment);
			document["bindingAbiRevision"] =
				static_cast<std::int64_t>(manifest.m_BindingABIRevision);
			document["coordinateOptions"] =
				static_cast<std::int64_t>(manifest.m_CoordinateOptions);
			document["stage"] = ShaderStageWire::Name(manifest.m_Stage);
			document["shaderModel"] = ShaderModelWire::Name(manifest.m_ShaderModel);
			document["hlslVersion"] = utils::ToString(manifest.m_HlslVersion);
			document["compileFlags"] = static_cast<std::int64_t>(manifest.m_CompileFlags);
			document["optimizationLevel"] = utils::ToString(manifest.m_OptimizationLevel);
			document["logicalSource"] =
				utils::ToString(manifest.m_LogicalSourcePath.generic_wstring());
			document["entryPoint"] = utils::ToString(manifest.m_EntryPoint);
			document["target"] = utils::ToString(manifest.m_TargetString);
			document["defines"] = nlohmann::json::array();
			for (const std::wstring& define : manifest.m_Defines)
			{
				document["defines"].push_back(utils::ToString(define));
			}
			document["logicalIncludeDirs"] = nlohmann::json::array();
			for (const std::filesystem::path& includeDir : manifest.m_LogicalIncludeDirs)
			{
				document["logicalIncludeDirs"].push_back(
					utils::ToString(includeDir.generic_wstring()));
			}
			document["extraArgs"] = nlohmann::json::array();
			for (const std::wstring& extraArg : manifest.m_ExtraArgs)
			{
				document["extraArgs"].push_back(utils::ToString(extraArg));
			}
			document["binaryContentDigest"] =
				Sha256DigestToHex(manifest.m_BinaryContentDigest.m_Digest);
			document["dependencies"] = nlohmann::json::array();
			for (const ShaderArtifactDependency& dependency : manifest.m_Dependencies)
			{
				nlohmann::json dependencyDocument;
				dependencyDocument["logicalPath"] =
					utils::ToString(dependency.m_LogicalPath.generic_wstring());
				dependencyDocument["contentDigest"] =
					Sha256DigestToHex(dependency.m_ContentDigest);
				document["dependencies"].push_back(std::move(dependencyDocument));
			}
			return document;
		}
	}

	std::optional<std::string> SerializeShaderArtifactManifest(
		const ShaderArtifactManifest& manifest) noexcept
	{
		try
		{
			return MakeManifestDocument(manifest).dump();
		}
		catch (...)
		{
			return std::nullopt;
		}
	}

	namespace
	{
		[[nodiscard]] std::optional<std::string> MakeCacheRecordJsonString(
			const ShaderArtifactCacheRecord& record) noexcept
		{
			try
			{
				nlohmann::json localDocument;
				localDocument["physicalSource"] =
					utils::ToString(utils::Canonical(record.m_PhysicalSourcePath).wstring());
				localDocument["physicalIncludeDirs"] = nlohmann::json::array();
				for (const std::filesystem::path& includeDir : record.m_PhysicalIncludeDirs)
				{
					localDocument["physicalIncludeDirs"].push_back(
						utils::ToString(utils::Canonical(includeDir).wstring()));
				}
				localDocument["dependencyPhysicalPaths"] = nlohmann::json::array();
				for (const std::filesystem::path& dependencyPhysicalPath :
					record.m_DependencyPhysicalPaths)
				{
					localDocument["dependencyPhysicalPaths"].push_back(
						utils::ToString(utils::Canonical(dependencyPhysicalPath).wstring()));
				}

				nlohmann::json document;
				document["recordSchemaVersion"] =
					static_cast<std::int64_t>(ShaderArtifactCacheRecordSchemaVersion);
				document["manifest"] = MakeManifestDocument(record.m_Manifest);
				document["local"] = std::move(localDocument);
				return document.dump();
			}
			catch (...)
			{
				return std::nullopt;
			}
		}
	}

	bool WriteShaderArtifactCacheRecord(const std::filesystem::path& recordPath,
		const ShaderArtifactCacheRecord& record) noexcept
	{
		if (!utils::CreateParentDirectoryIfNotExist(recordPath))
		{
			return false;
		}

		const std::optional<std::string> serialized = MakeCacheRecordJsonString(record);
		if (!serialized.has_value())
		{
			return false;
		}

		try
		{
			std::ofstream out(recordPath, std::ios::binary);
			if (!out)
			{
				return false;
			}
			out.write(serialized->data(), static_cast<std::streamsize>(serialized->size()));
			return static_cast<bool>(out);
		}
		catch (...)
		{
			return false;
		}
	}

	namespace
	{
		// SAX validator enforcing the shader serialization strictness rules that nlohmann/json
		// does not enforce by itself: duplicate object keys are rejected and an
		// explicit nesting depth limit (64) bounds recursion. Returning false
		// from any event makes sax_parse report failure, which the reader maps
		// to a cache miss. The validator runs before the DOM parse, so the DOM
		// parse itself is bounded by this pre-check. Cache-record and manifest-only
		// readers share this gate.
		class StrictJsonSax final : public nlohmann::json_sax<nlohmann::json>
		{
		public:
			bool null() override { return true; }
			bool boolean(bool /*value*/) override { return true; }
			bool number_integer(nlohmann::json::number_integer_t /*value*/) override
			{
				return true;
			}
			bool number_unsigned(nlohmann::json::number_unsigned_t /*value*/) override
			{
				return true;
			}
			bool number_float(nlohmann::json::number_float_t /*value*/,
				const nlohmann::json::string_t& /*representation*/) override
			{
				return true;
			}
			bool string(nlohmann::json::string_t& /*value*/) override
			{
				return true;
			}
			bool binary(nlohmann::json::binary_t& /*value*/) override
			{
				return false; // binary payloads are outside the cache record contract
			}
			bool parse_error(std::size_t /*position*/,
				const std::string& /*lastToken*/,
				const nlohmann::detail::exception& /*exception*/) override
			{
				return false;
			}
			bool start_object(std::size_t /*elements*/) override
			{
				return EnterScope(true);
			}
			bool start_array(std::size_t /*elements*/) override
			{
				return EnterScope(false);
			}
			bool key(nlohmann::json::string_t& value) override
			{
				if (m_ScopeIsObject.empty() || !m_ScopeIsObject.back())
				{
					return false;
				}
				return m_KeyScopes.back().insert(value).second;
			}
			bool end_object() override
			{
				return ExitScope();
			}
			bool end_array() override
			{
				return ExitScope();
			}

		private:
			[[nodiscard]] bool EnterScope(bool isObject) noexcept
			{
				if (m_Depth >= MaxNestingDepth)
				{
					return false;
				}
				++m_Depth;
				m_KeyScopes.emplace_back();
				m_ScopeIsObject.push_back(isObject);
				return true;
			}
			[[nodiscard]] bool ExitScope() noexcept
			{
				if (m_ScopeIsObject.empty())
				{
					return false;
				}
				--m_Depth;
				m_KeyScopes.pop_back();
				m_ScopeIsObject.pop_back();
				return true;
			}

			static constexpr int MaxNestingDepth = 64;
			int m_Depth = 0;
			std::vector<std::unordered_set<std::string>> m_KeyScopes;
			std::vector<bool> m_ScopeIsObject;
		};

		[[nodiscard]] std::optional<nlohmann::json> ParseJsonDocumentStrict(
			std::string_view serializedDocument) noexcept
		{
			try
			{
				const std::string content(serializedDocument);
				StrictJsonSax strictSax;
				if (!nlohmann::json::sax_parse(content, &strictSax))
				{
					return std::nullopt;
				}
				return nlohmann::json::parse(content);
			}
			catch (...)
			{
				return std::nullopt;
			}
		}

		[[nodiscard]] const nlohmann::json* FindField(
			const nlohmann::json& object, std::string_view key) noexcept
		{
			if (!object.is_object())
			{
				return nullptr;
			}
			const auto found = object.find(std::string(key));
			return found != object.end() ? &*found : nullptr;
		}

		[[nodiscard]] bool ParseRequiredInteger(const nlohmann::json& object,
			std::string_view key, int64_t& outValue) noexcept
		{
			const nlohmann::json* field = FindField(object, key);
			if (field == nullptr || !field->is_number())
			{
				return false;
			}
			// is_number_integer() covers both signed and unsigned storage, and
			// get<int64_t> on an unsigned value above INT64_MAX is an
			// arithmetic conversion, not a rejection. Check the unsigned
			// storage first so values outside the int64 domain are rejected.
			if (field->is_number_unsigned())
			{
				const std::uint64_t value = field->get<std::uint64_t>();
				if (value >
					static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
				{
					return false;
				}
				outValue = static_cast<std::int64_t>(value);
				return true;
			}
			if (field->is_number_integer())
			{
				outValue = field->get<std::int64_t>();
				return true;
			}
			return false; // floating-point numbers are outside the integer contract
		}

		[[nodiscard]] bool ParseRequiredString(const nlohmann::json& object,
			std::string_view key, std::string& outValue) noexcept
		{
			const nlohmann::json* field = FindField(object, key);
			if (field == nullptr || !field->is_string())
			{
				return false;
			}
			outValue = field->get<std::string>();
			return true;
		}

		[[nodiscard]] bool ParseRequiredStringArray(const nlohmann::json& object,
			std::string_view key, std::vector<std::string>& outValues) noexcept
		{
			const nlohmann::json* field = FindField(object, key);
			if (field == nullptr || !field->is_array())
			{
				return false;
			}
			for (const nlohmann::json& element : *field)
			{
				if (!element.is_string())
				{
					return false;
				}
				outValues.push_back(element.get<std::string>());
			}
			return true;
		}

		struct CacheRecordJsonMapper final
		{
			[[nodiscard]] std::optional<ShaderArtifactCacheRecord> Map(
				const nlohmann::json& root) noexcept
			{
				if (!root.is_object())
				{
					return std::nullopt;
				}

				int64_t recordSchemaVersion = 0;
				if (!ParseRequiredInteger(root, "recordSchemaVersion", recordSchemaVersion) ||
					recordSchemaVersion != ShaderArtifactCacheRecordSchemaVersion)
				{
					return std::nullopt;
				}

				const nlohmann::json* manifestValue = FindField(root, "manifest");
				const nlohmann::json* localValue = FindField(root, "local");
				if (manifestValue == nullptr || !manifestValue->is_object() ||
					localValue == nullptr || !localValue->is_object())
				{
					return std::nullopt;
				}
				if (root.size() != 3)
				{
					return std::nullopt; // unknown keys rejected: schema bump required
				}

				ShaderArtifactCacheRecord record{};
				if (!MapManifest(*manifestValue, record.m_Manifest) ||
					!MapLocal(*localValue, record))
				{
					return std::nullopt;
				}
				// Structural main-source binding and dependency cardinality
				// invariants: the main source must be present and described by
				// the first dependency, in both its portable (logical) and
				// local (canonical physical) forms, and every portable
				// dependency must pair with exactly one local physical
				// resolution. Violations are invalid derived data.
				if (record.m_Manifest.m_Dependencies.empty() ||
					record.m_Manifest.m_Dependencies.size() !=
						record.m_DependencyPhysicalPaths.size() ||
					record.m_Manifest.m_Dependencies[0].m_LogicalPath !=
						record.m_Manifest.m_LogicalSourcePath ||
					utils::Canonical(record.m_DependencyPhysicalPaths[0]) !=
						utils::Canonical(record.m_PhysicalSourcePath))
				{
					return std::nullopt;
				}
				return record;
			}

		public:
			[[nodiscard]] bool MapManifest(const nlohmann::json& object,
				ShaderArtifactManifest& manifest) noexcept
			{
				std::string text;
				int64_t integer = 0;
				std::vector<std::string> strings;
				uint32_t seenMask = 0;
				constexpr uint32_t AllBits = (1u << 23) - 1;

				if (!ParseRequiredInteger(object, "schemaVersion", integer) ||
					integer != ShaderArtifactManifestSchemaVersion)
				{
					return false;
				}
				manifest.m_SchemaVersion = static_cast<uint32_t>(integer);
				seenMask |= 1u << 0;
				if (!ParseRequiredInteger(object, "recipeHashSchema", integer) ||
					integer != ShaderRecipeHashSchema)
				{
					return false;
				}
				manifest.m_RecipeHashSchema = static_cast<uint32_t>(integer);
				seenMask |= 1u << 1;
				if (!ParseRequiredString(object, "recipeId", text))
				{
					return false;
				}
				const auto recipeId = ParseHexSha256Digest(text);
				if (!recipeId.has_value())
				{
					return false;
				}
				manifest.m_RecipeId.m_DurableDigest = *recipeId;
				seenMask |= 1u << 2;
				if (!ParseRequiredString(object, "buildKey", text))
				{
					return false;
				}
				const auto buildKey = ParseHexSha256Digest(text);
				if (!buildKey.has_value())
				{
					return false;
				}
				manifest.m_BuildKey.m_DurableDigest = *buildKey;
				seenMask |= 1u << 3;

				const nlohmann::json* compiler = FindField(object, "compiler");
				if (compiler == nullptr || !compiler->is_object())
				{
					return false;
				}
				bool hasKind = false;
				bool hasIdentity = false;
				for (const auto& member : compiler->items())
				{
					if (member.key() == "kind")
					{
						if (!member.value().is_string() ||
							member.value().get<std::string>() != "dxc")
						{
							return false;
						}
						hasKind = true;
					}
					else if (member.key() == "identity")
					{
						if (!member.value().is_string())
						{
							return false;
						}
						manifest.m_CompilerIdentity.m_CanonicalIdentity =
							utils::ToWideString(member.value().get<std::string>());
						hasIdentity = true;
					}
					else
					{
						return false;
					}
				}
				if (!hasKind || !hasIdentity)
				{
					return false;
				}
				seenMask |= 1u << 4;

				if (!ParseRequiredString(object, "targetProfile", text) ||
					!ShaderTargetWire::Parse(text, manifest.m_TargetProfile))
				{
					return false;
				}
				seenMask |= 1u << 5;
				if (!ParseRequiredString(object, "binaryFormat", text) ||
					!ShaderBinaryFormatWire::Parse(text, manifest.m_BinaryFormat))
				{
					return false;
				}
				seenMask |= 1u << 6;
				if (!ParseRequiredString(object, "spirvTargetEnvironment", text) ||
					!ShaderSpirVTargetEnvironmentWire::Parse(
						text, manifest.m_SpirVTargetEnvironment))
				{
					return false;
				}
				seenMask |= 1u << 7;
				if (!ParseRequiredInteger(object, "bindingAbiRevision", integer) ||
					integer < 0 || integer > UINT32_MAX)
				{
					return false;
				}
				manifest.m_BindingABIRevision = static_cast<uint32_t>(integer);
				seenMask |= 1u << 8;
				if (!ParseRequiredInteger(object, "coordinateOptions", integer) ||
					integer < 0 || integer > UINT32_MAX)
				{
					return false;
				}
				manifest.m_CoordinateOptions =
					static_cast<ShaderCoordinateOptions>(integer);
				seenMask |= 1u << 9;
				if (!ParseRequiredString(object, "stage", text) ||
					!ShaderStageWire::Parse(text, manifest.m_Stage))
				{
					return false;
				}
				seenMask |= 1u << 10;
				if (!ParseRequiredString(object, "shaderModel", text) ||
					!ShaderModelWire::Parse(text, manifest.m_ShaderModel))
				{
					return false;
				}
				seenMask |= 1u << 11;
				if (!ParseRequiredString(object, "hlslVersion", text))
				{
					return false;
				}
				manifest.m_HlslVersion = utils::ToWideString(text);
				seenMask |= 1u << 12;
				if (!ParseRequiredInteger(object, "compileFlags", integer) ||
					integer < 0 || integer > UINT32_MAX)
				{
					return false;
				}
				manifest.m_CompileFlags = static_cast<ShaderCompileFlags>(integer);
				seenMask |= 1u << 13;
				if (!ParseRequiredString(object, "optimizationLevel", text))
				{
					return false;
				}
				manifest.m_OptimizationLevel = utils::ToWideString(text);
				seenMask |= 1u << 14;
				if (!ParseRequiredString(object, "logicalSource", text))
				{
					return false;
				}
				manifest.m_LogicalSourcePath = utils::ToWideString(text);
				seenMask |= 1u << 15;
				if (!ParseRequiredString(object, "entryPoint", text))
				{
					return false;
				}
				manifest.m_EntryPoint = utils::ToWideString(text);
				seenMask |= 1u << 16;
				if (!ParseRequiredString(object, "target", text))
				{
					return false;
				}
				manifest.m_TargetString = utils::ToWideString(text);
				seenMask |= 1u << 17;
				if (!ParseRequiredStringArray(object, "defines", strings))
				{
					return false;
				}
				for (const std::string& define : strings)
				{
					manifest.m_Defines.push_back(utils::ToWideString(define));
				}
				seenMask |= 1u << 18;
				strings.clear();
				if (!ParseRequiredStringArray(object, "logicalIncludeDirs", strings))
				{
					return false;
				}
				for (const std::string& includeDir : strings)
				{
					manifest.m_LogicalIncludeDirs.emplace_back(utils::ToWideString(includeDir));
				}
				seenMask |= 1u << 19;
				strings.clear();
				if (!ParseRequiredStringArray(object, "extraArgs", strings))
				{
					return false;
				}
				for (const std::string& extraArg : strings)
				{
					manifest.m_ExtraArgs.push_back(utils::ToWideString(extraArg));
				}
				seenMask |= 1u << 20;
				if (!ParseRequiredString(object, "binaryContentDigest", text))
				{
					return false;
				}
				const auto digest = ParseHexSha256Digest(text);
				if (!digest.has_value())
				{
					return false;
				}
				manifest.m_BinaryContentDigest.m_Digest = *digest;
				seenMask |= 1u << 21;

				const nlohmann::json* dependencies = FindField(object, "dependencies");
				if (dependencies == nullptr || !dependencies->is_array())
				{
					return false;
				}
				for (const nlohmann::json& dependencyValue : *dependencies)
				{
					if (!dependencyValue.is_object())
					{
						return false;
					}
					ShaderArtifactDependency dependency{};
					std::string logicalPath;
					std::string contentDigest;
					if (!ParseRequiredString(dependencyValue, "logicalPath", logicalPath) ||
						!ParseRequiredString(dependencyValue, "contentDigest", contentDigest) ||
						dependencyValue.size() != 2)
					{
						return false;
					}
					const auto parsedDigest = ParseHexSha256Digest(contentDigest);
					if (!parsedDigest.has_value())
					{
						return false;
					}
					dependency.m_LogicalPath = utils::ToWideString(logicalPath);
					dependency.m_ContentDigest = *parsedDigest;
					manifest.m_Dependencies.push_back(std::move(dependency));
				}
				seenMask |= 1u << 22;

				return seenMask == AllBits && object.size() == 23 &&
					!manifest.m_Dependencies.empty() &&
					manifest.m_Dependencies[0].m_LogicalPath == manifest.m_LogicalSourcePath;
			}

		private:
			[[nodiscard]] bool MapLocal(const nlohmann::json& object,
				ShaderArtifactCacheRecord& record) noexcept
			{
				std::string text;
				std::vector<std::string> strings;
				if (!ParseRequiredString(object, "physicalSource", text))
				{
					return false;
				}
				record.m_PhysicalSourcePath = utils::ToWideString(text);
				if (!ParseRequiredStringArray(object, "physicalIncludeDirs", strings))
				{
					return false;
				}
				for (const std::string& includeDir : strings)
				{
					record.m_PhysicalIncludeDirs.emplace_back(utils::ToWideString(includeDir));
				}
				strings.clear();
				if (!ParseRequiredStringArray(object, "dependencyPhysicalPaths", strings))
				{
					return false;
				}
				for (const std::string& dependencyPhysicalPath : strings)
				{
					record.m_DependencyPhysicalPaths.emplace_back(
						utils::ToWideString(dependencyPhysicalPath));
				}
				return object.size() == 3;
			}
		};
	}

	std::optional<ShaderArtifactManifest> DeserializeShaderArtifactManifest(
		std::string_view serializedManifest) noexcept
	{
		const std::optional<nlohmann::json> document =
			ParseJsonDocumentStrict(serializedManifest);
		if (!document.has_value() || !document->is_object())
		{
			return std::nullopt;
		}
		ShaderArtifactManifest manifest{};
		if (!CacheRecordJsonMapper{}.MapManifest(*document, manifest))
		{
			return std::nullopt;
		}
		return manifest;
	}

	std::optional<ShaderArtifactCacheRecord> ReadShaderArtifactCacheRecord(
		const std::filesystem::path& recordPath) noexcept
	{
		std::ifstream in(recordPath, std::ios::binary);
		if (!in)
		{
			return std::nullopt;
		}
		const std::string content((std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());

		const std::optional<nlohmann::json> document = ParseJsonDocumentStrict(content);
		if (!document.has_value())
		{
			return std::nullopt;
		}
		return CacheRecordJsonMapper{}.Map(*document);
	}

	std::optional<ShaderArtifactCacheRecord> LoadShaderArtifactCacheRecord(
		const std::filesystem::path& recordPath,
		const std::filesystem::path& binaryPath) noexcept
	{
		std::error_code errorCode;
		if (!std::filesystem::exists(recordPath, errorCode) ||
			!std::filesystem::exists(binaryPath, errorCode))
		{
			return std::nullopt;
		}

		std::optional<ShaderArtifactCacheRecord> record =
			ReadShaderArtifactCacheRecord(recordPath);
		if (!record.has_value())
		{
			return std::nullopt;
		}

		// The binary is read exactly once through the single read point: the
		// manifest digest is compared against the SHA-256 of those exact
		// in-memory bytes, and those same bytes are returned. Validation and
		// return content can never diverge.
		const testing::BinaryReadOnceOverride readOnce = (g_BinaryReadOnceOverride != nullptr)
			? g_BinaryReadOnceOverride
			: &testing::ReadBinaryWithDigestOnce;
		const std::optional<testing::BinaryReadWithDigest> loaded = readOnce(binaryPath);
		if (!loaded.has_value() ||
			loaded->m_Digest != record->m_Manifest.m_BinaryContentDigest.m_Digest)
		{
			return std::nullopt;
		}

		record->m_Binary = loaded->m_Binary;
		return record;
	}

	namespace
	{
		struct CacheRecordObservationContext
		{
			std::filesystem::path m_RecordPath;
			std::filesystem::path m_BinaryPath;
			std::optional<ShaderArtifactCacheRecord> m_Observed;
		};

		[[nodiscard]] bool ObserveCacheRecord(void* context) noexcept
		{
			auto* observation = static_cast<CacheRecordObservationContext*>(context);
			// Load validates the record and hashes the exact binary bytes once,
			// so a positive observation is a complete committed-entry check.
			observation->m_Observed = LoadShaderArtifactCacheRecord(
				observation->m_RecordPath, observation->m_BinaryPath);
			return observation->m_Observed.has_value();
		}
	}

	ShaderPublicationResult PublishShaderArtifactCacheRecord(
		const std::filesystem::path& binaryPath,
		const std::filesystem::path& recordPath,
		const ShaderArtifactCacheRecord& record) noexcept
	{
		ShaderPublicationResult result{};
		try
		{
			const std::optional<std::string> serializedRecord =
				MakeCacheRecordJsonString(record);
			if (!serializedRecord.has_value())
			{
				return result;
			}

			const auto binaryBytes = std::span(
				static_cast<const std::byte*>(record.m_Binary.Data()),
				record.m_Binary.SizeInBytes());
			const auto recordBytes = std::span(
				reinterpret_cast<const std::byte*>(serializedRecord->data()),
				serializedRecord->size());

			// Validate the complete result before publication: the manifest
			// digest must equal SHA-256 of the exact binary bytes about to be
			// published. Own submission commits the immutable binary first and
			// the cache record last as the commit marker; rename failures do
			// not pre-judge the winner, because the final committed-entry
			// observation classifies the outcome alone.
			CacheRecordObservationContext observation{
				.m_RecordPath = recordPath,
				.m_BinaryPath = binaryPath,
			};
			const std::array files{
				PublicationFileSpec{
					.m_Destination = binaryPath,
					.m_Content = binaryBytes,
					.m_Verification = PublicationVerification::ContentDigest,
					.m_ExpectedDigest = record.m_Manifest.m_BinaryContentDigest.m_Digest,
				},
				PublicationFileSpec{
					.m_Destination = recordPath,
					.m_Content = recordBytes,
					.m_Verification = PublicationVerification::None,
				},
			};
			const PublicationTransactionResult transaction = ExecutePublicationTransaction(
				files,
				{
					.m_Commit = PublicationCommit::ExclusiveRename,
					.m_RecoverOrphanedPrimary = true,
					.m_MaxAttempts = 1,
					.m_ObservationAttempts = 64,
					.m_RenameFailureInjector = g_PublishFileFailureInjector,
					.m_Observer = {
						.m_Context = &observation,
						.m_Observe = &ObserveCacheRecord,
					},
				});
			if (!transaction.m_Observed || !observation.m_Observed.has_value())
			{
				return result; // Failed
			}
			// Structural binding beyond the load: the observed entry must belong
			// to this slot's recipe/producer identity. A non-equivalent entry can
			// never be disguised as a winner.
			const ShaderArtifactCacheRecord& observed = *observation.m_Observed;
			if (observed.m_Manifest.m_RecipeId != record.m_Manifest.m_RecipeId ||
				observed.m_Manifest.m_BuildKey != record.m_Manifest.m_BuildKey)
			{
				return result; // Failed
			}

			result.m_CommittedRecord = observed;
			result.m_Outcome = (observed.m_Manifest == record.m_Manifest)
				? ShaderPublicationOutcome::Published
				: ShaderPublicationOutcome::CommittedByOther;
			return result;
		}
		catch (...)
		{
			return result;
		}
	}
}
