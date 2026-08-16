#include "Artifact/ShaderArtifactManifestIO.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"

#include <nlohmann/json.hpp>

#include <process.h>

#include <atomic>
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
		[[nodiscard]] constexpr std::string_view ShaderBinaryFormatText(
			ShaderBinaryFormat format) noexcept
		{
			switch (format)
			{
			case ShaderBinaryFormat::Dxil:
				return "dxil";
			case ShaderBinaryFormat::SpirV:
				return "spirv";
			case ShaderBinaryFormat::Unknown:
				break;
			}
			return "unknown";
		}

		[[nodiscard]] constexpr bool ParseShaderBinaryFormat(
			std::string_view text, ShaderBinaryFormat& outFormat) noexcept
		{
			if (text == "dxil")
			{
				outFormat = ShaderBinaryFormat::Dxil;
				return true;
			}
			if (text == "spirv")
			{
				outFormat = ShaderBinaryFormat::SpirV;
				return true;
			}
			return false;
		}

		[[nodiscard]] constexpr std::string_view SpirVTargetEnvironmentText(
			ShaderSpirVTargetEnvironment environment) noexcept
		{
			switch (environment)
			{
			case ShaderSpirVTargetEnvironment::None:
				return "none";
			case ShaderSpirVTargetEnvironment::Vulkan1_3:
				return "vulkan1.3";
			}
			return "unknown";
		}

		[[nodiscard]] constexpr bool ParseSpirVTargetEnvironment(
			std::string_view text, ShaderSpirVTargetEnvironment& outEnvironment) noexcept
		{
			if (text == "none")
			{
				outEnvironment = ShaderSpirVTargetEnvironment::None;
				return true;
			}
			if (text == "vulkan1.3")
			{
				outEnvironment = ShaderSpirVTargetEnvironment::Vulkan1_3;
				return true;
			}
			return false;
		}

		[[nodiscard]] constexpr std::string_view ShaderStageText(ShaderStage stage) noexcept
		{
			switch (stage)
			{
			case ShaderStage::Vertex:
				return "vertex";
			case ShaderStage::Pixel:
				return "pixel";
			case ShaderStage::Hull:
				return "hull";
			case ShaderStage::Domain:
				return "domain";
			case ShaderStage::Geometry:
				return "geometry";
			case ShaderStage::Mesh:
				return "mesh";
			case ShaderStage::Compute:
				return "compute";
			}
			return "unknown";
		}

		[[nodiscard]] constexpr bool ParseShaderStage(
			std::string_view text, ShaderStage& outStage) noexcept
		{
			if (text == "vertex")
			{
				outStage = ShaderStage::Vertex;
				return true;
			}
			if (text == "pixel")
			{
				outStage = ShaderStage::Pixel;
				return true;
			}
			if (text == "hull")
			{
				outStage = ShaderStage::Hull;
				return true;
			}
			if (text == "domain")
			{
				outStage = ShaderStage::Domain;
				return true;
			}
			if (text == "geometry")
			{
				outStage = ShaderStage::Geometry;
				return true;
			}
			if (text == "mesh")
			{
				outStage = ShaderStage::Mesh;
				return true;
			}
			if (text == "compute")
			{
				outStage = ShaderStage::Compute;
				return true;
			}
			return false;
		}

		[[nodiscard]] constexpr std::string_view ShaderModelText(ShaderModel model) noexcept
		{
			switch (model)
			{
			case ShaderModel::SM_6_6:
				return "6_6";
			case ShaderModel::SM_6_7:
				return "6_7";
			case ShaderModel::SM_6_8:
				return "6_8";
			}
			return "unknown";
		}

		[[nodiscard]] constexpr bool ParseShaderModel(
			std::string_view text, ShaderModel& outModel) noexcept
		{
			if (text == "6_6")
			{
				outModel = ShaderModel::SM_6_6;
				return true;
			}
			if (text == "6_7")
			{
				outModel = ShaderModel::SM_6_7;
				return true;
			}
			if (text == "6_8")
			{
				outModel = ShaderModel::SM_6_8;
				return true;
			}
			return false;
		}

		[[nodiscard]] constexpr std::string_view ShaderTargetProfileText(
			ShaderTargetProfile profile) noexcept
		{
			switch (profile)
			{
			case ShaderTargetProfile::GGLabDX12:
				return "gglab-dx12";
			case ShaderTargetProfile::GGLabVulkan13:
				return "gglab-vulkan13";
			}
			return "unknown";
		}

		[[nodiscard]] constexpr bool ParseShaderTargetProfile(
			std::string_view text, ShaderTargetProfile& outProfile) noexcept
		{
			if (text == "gglab-dx12")
			{
				outProfile = ShaderTargetProfile::GGLabDX12;
				return true;
			}
			if (text == "gglab-vulkan13")
			{
				outProfile = ShaderTargetProfile::GGLabVulkan13;
				return true;
			}
			return false;
		}

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

		[[nodiscard]] std::optional<BinaryContentDigest> ComputeFileContentDigest(
			const std::filesystem::path& path) noexcept
		{
			const std::optional<ShaderBinary> binary = ReadFileBinary(path);
			if (!binary.has_value())
			{
				return std::nullopt;
			}
			BinaryContentDigest digest{};
			digest.m_Digest = ComputeSha256(std::span(
				static_cast<const std::byte*>(binary->Data()), binary->SizeInBytes()));
			return digest;
		}

		[[nodiscard]] std::filesystem::path MakeUniqueTempPath(
			const std::filesystem::path& destination) noexcept
		{
			static std::atomic_uint64_t counter = 0;
			return destination.wstring() + L".tmp." +
				std::to_wstring(static_cast<uint32_t>(::_getpid())) + L"." +
				std::to_wstring(counter.fetch_add(1, std::memory_order_relaxed));
		}

		void RemoveFileBestEffort(const std::filesystem::path& path) noexcept
		{
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}

		[[nodiscard]] bool PublishFile(
			const std::filesystem::path& source, const std::filesystem::path& destination) noexcept
		{
			std::error_code errorCode;
			std::filesystem::rename(source, destination, errorCode);
			return !errorCode;
		}
	}

	namespace
	{
		// Null selects the production read-once implementation.
		BinaryReadOnceOverride g_BinaryReadOnceOverride = nullptr;
	}

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

	void OverrideBinaryReadOnceForTest(BinaryReadOnceOverride overrideFn) noexcept
	{
		g_BinaryReadOnceOverride = overrideFn;
	}

	bool WriteShaderArtifactCacheRecord(const std::filesystem::path& manifestPath,
		const ShaderArtifactCacheRecord& record) noexcept
	{
		const bool created = utils::CreateParentDirectoryIfNotExist(manifestPath);
		if (!created)
		{
			return false;
		}

		const ShaderArtifactManifest& manifest = record.m_Manifest;

		nlohmann::json manifestDocument;
		manifestDocument["schemaVersion"] = static_cast<std::int64_t>(manifest.m_SchemaVersion);		manifestDocument["recipeHashSchema"] = static_cast<std::int64_t>(manifest.m_RecipeHashSchema);
		manifestDocument["recipeId"] = Sha256DigestToHex(manifest.m_RecipeId.m_DurableDigest);
		manifestDocument["buildKey"] = Sha256DigestToHex(manifest.m_BuildKey.m_DurableDigest);
		nlohmann::json compiler;
		compiler["kind"] = "dxc";
		compiler["identity"] = utils::ToString(manifest.m_CompilerIdentity.m_CanonicalIdentity);
		manifestDocument["compiler"] = std::move(compiler);
		manifestDocument["targetProfile"] = ShaderTargetProfileText(manifest.m_TargetProfile);
		manifestDocument["binaryFormat"] = ShaderBinaryFormatText(manifest.m_BinaryFormat);
		manifestDocument["spirvTargetEnvironment"] =
			SpirVTargetEnvironmentText(manifest.m_SpirVTargetEnvironment);
		manifestDocument["bindingAbiRevision"] =
			static_cast<std::int64_t>(manifest.m_BindingABIRevision);
		manifestDocument["coordinateOptions"] =
			static_cast<std::int64_t>(manifest.m_CoordinateOptions);
		manifestDocument["stage"] = ShaderStageText(manifest.m_Stage);
		manifestDocument["shaderModel"] = ShaderModelText(manifest.m_ShaderModel);
		manifestDocument["hlslVersion"] = utils::ToString(manifest.m_HlslVersion);
		manifestDocument["compileFlags"] = static_cast<std::int64_t>(manifest.m_CompileFlags);
		manifestDocument["optimizationLevel"] = utils::ToString(manifest.m_OptimizationLevel);
		manifestDocument["logicalSource"] =
			utils::ToString(manifest.m_LogicalSourcePath.generic_wstring());
		manifestDocument["entryPoint"] = utils::ToString(manifest.m_EntryPoint);
		manifestDocument["target"] = utils::ToString(manifest.m_TargetString);
		manifestDocument["defines"] = nlohmann::json::array();
		for (const std::wstring& define : manifest.m_Defines)
		{
			manifestDocument["defines"].push_back(utils::ToString(define));
		}
		manifestDocument["logicalIncludeDirs"] = nlohmann::json::array();
		for (const std::filesystem::path& includeDir : manifest.m_LogicalIncludeDirs)
		{
			manifestDocument["logicalIncludeDirs"].push_back(
				utils::ToString(includeDir.generic_wstring()));
		}
		manifestDocument["extraArgs"] = nlohmann::json::array();
		for (const std::wstring& extraArg : manifest.m_ExtraArgs)
		{
			manifestDocument["extraArgs"].push_back(utils::ToString(extraArg));
		}
		manifestDocument["binaryContentDigest"] =
			Sha256DigestToHex(manifest.m_BinaryContentDigest.m_Digest);
		manifestDocument["dependencies"] = nlohmann::json::array();
		for (const ShaderArtifactDependency& dependency : manifest.m_Dependencies)
		{
			nlohmann::json dependencyDocument;
			dependencyDocument["logicalPath"] =
				utils::ToString(dependency.m_LogicalPath.generic_wstring());
			dependencyDocument["contentDigest"] =
				Sha256DigestToHex(dependency.m_ContentDigest);
			manifestDocument["dependencies"].push_back(std::move(dependencyDocument));
		}

		nlohmann::json localDocument;
		localDocument["physicalSource"] = utils::Canonical(record.m_PhysicalSourcePath).string();
		localDocument["physicalIncludeDirs"] = nlohmann::json::array();
		for (const std::filesystem::path& includeDir : record.m_PhysicalIncludeDirs)
		{
			localDocument["physicalIncludeDirs"].push_back(
				utils::Canonical(includeDir).string());
		}
		localDocument["dependencyPhysicalPaths"] = nlohmann::json::array();
		for (const std::filesystem::path& dependencyPhysicalPath :
			record.m_DependencyPhysicalPaths)
		{
			localDocument["dependencyPhysicalPaths"].push_back(
				utils::Canonical(dependencyPhysicalPath).string());
		}

		nlohmann::json document;
		document["recordSchemaVersion"] =
			static_cast<std::int64_t>(ShaderArtifactCacheRecordSchemaVersion);
		document["manifest"] = std::move(manifestDocument);
		document["local"] = std::move(localDocument);

		std::ofstream out(manifestPath, std::ios::binary);
		if (!out)
		{
			return false;
		}
		const std::string content = document.dump();
		out.write(content.data(), static_cast<std::streamsize>(content.size()));
		return static_cast<bool>(out);
	}

	namespace
	{
		// SAX validator enforcing the domain strictness rules that nlohmann/json
		// does not enforce by itself: duplicate object keys are rejected and an
		// explicit nesting depth limit (64) bounds recursion. Returning false
		// from any event makes sax_parse report failure, which the reader maps
		// to a cache miss. The validator runs before the DOM parse, so the DOM
		// parse itself is bounded by this pre-check.
		class StrictJsonSax final : public nlohmann::json_sax<nlohmann::json>
		{
		public:
			[[nodiscard]] bool null() override { return true; }
			[[nodiscard]] bool boolean(bool /*value*/) override { return true; }
			[[nodiscard]] bool number_integer(nlohmann::json::number_integer_t /*value*/) override
			{
				return true;
			}
			[[nodiscard]] bool number_unsigned(nlohmann::json::number_unsigned_t /*value*/) override
			{
				return true;
			}
			[[nodiscard]] bool number_float(nlohmann::json::number_float_t /*value*/,
				const nlohmann::json::string_t& /*representation*/) override
			{
				return true;
			}
			[[nodiscard]] bool string(nlohmann::json::string_t& /*value*/) override
			{
				return true;
			}
			[[nodiscard]] bool binary(nlohmann::json::binary_t& /*value*/) override
			{
				return false; // binary payloads are outside the cache record contract
			}
			[[nodiscard]] bool parse_error(std::size_t /*position*/,
				const std::string& /*lastToken*/,
				const nlohmann::detail::exception& /*exception*/) override
			{
				return false;
			}
			[[nodiscard]] bool start_object(std::size_t /*elements*/) override
			{
				return EnterScope(true);
			}
			[[nodiscard]] bool start_array(std::size_t /*elements*/) override
			{
				return EnterScope(false);
			}
			[[nodiscard]] bool key(nlohmann::json::string_t& value) override
			{
				if (m_ScopeIsObject.empty() || !m_ScopeIsObject.back())
				{
					return false;
				}
				return m_KeyScopes.back().insert(value).second;
			}
			[[nodiscard]] bool end_object() override
			{
				return ExitScope();
			}
			[[nodiscard]] bool end_array() override
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
				// Dependency cardinality invariant: every portable dependency
				// must pair with exactly one local physical resolution.
				if (record.m_Manifest.m_Dependencies.size() !=
					record.m_DependencyPhysicalPaths.size())
				{
					return std::nullopt;
				}
				return record;
			}

		private:
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
					!ParseShaderTargetProfile(text, manifest.m_TargetProfile))
				{
					return false;
				}
				seenMask |= 1u << 5;
				if (!ParseRequiredString(object, "binaryFormat", text) ||
					!ParseShaderBinaryFormat(text, manifest.m_BinaryFormat))
				{
					return false;
				}
				seenMask |= 1u << 6;
				if (!ParseRequiredString(object, "spirvTargetEnvironment", text) ||
					!ParseSpirVTargetEnvironment(text, manifest.m_SpirVTargetEnvironment))
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
					!ParseShaderStage(text, manifest.m_Stage))
				{
					return false;
				}
				seenMask |= 1u << 10;
				if (!ParseRequiredString(object, "shaderModel", text) ||
					!ParseShaderModel(text, manifest.m_ShaderModel))
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

				return seenMask == AllBits && object.size() == 23;
			}

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

	std::optional<ShaderArtifactCacheRecord> ReadShaderArtifactCacheRecord(
		const std::filesystem::path& manifestPath) noexcept
	{
		std::ifstream in(manifestPath, std::ios::binary);
		if (!in)
		{
			return std::nullopt;
		}
		const std::string content((std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());

		try
		{
			StrictJsonSax strictSax;
			if (!nlohmann::json::sax_parse(content, &strictSax))
			{
				return std::nullopt;
			}
		}
		catch (const nlohmann::json::exception&)
		{
			return std::nullopt;
		}

		nlohmann::json document;
		try
		{
			document = nlohmann::json::parse(content);
		}
		catch (const nlohmann::json::exception&)
		{
			return std::nullopt;
		}
		return CacheRecordJsonMapper{}.Map(document);
	}

	std::optional<ShaderArtifactCacheRecord> LoadShaderArtifactCacheRecord(
		const std::filesystem::path& manifestPath,
		const std::filesystem::path& binaryPath) noexcept
	{
		std::error_code errorCode;
		if (!std::filesystem::exists(manifestPath, errorCode) ||
			!std::filesystem::exists(binaryPath, errorCode))
		{
			return std::nullopt;
		}

		std::optional<ShaderArtifactCacheRecord> record =
			ReadShaderArtifactCacheRecord(manifestPath);
		if (!record.has_value())
		{
			return std::nullopt;
		}

		// The binary is read exactly once through the single read point: the
		// manifest digest is compared against the SHA-256 of those exact
		// in-memory bytes, and those same bytes are returned. Validation and
		// return content can never diverge.
		const BinaryReadOnceOverride readOnce = (g_BinaryReadOnceOverride != nullptr)
			? g_BinaryReadOnceOverride
			: &ReadBinaryWithDigestOnce;
		const std::optional<BinaryReadWithDigest> loaded = readOnce(binaryPath);
		if (!loaded.has_value() ||
			loaded->m_Digest != record->m_Manifest.m_BinaryContentDigest.m_Digest)
		{
			return std::nullopt;
		}

		record->m_Binary = loaded->m_Binary;
		return record;
	}

	bool PublishShaderArtifactCacheRecord(const std::filesystem::path& binaryPath,
		const std::filesystem::path& manifestPath, const ShaderArtifactCacheRecord& record) noexcept
	{
		const bool parentsReady = utils::CreateParentDirectoryIfNotExist(binaryPath) &&
			utils::CreateParentDirectoryIfNotExist(manifestPath);
		if (!parentsReady)
		{
			return false;
		}

		const std::filesystem::path tempBinaryPath = MakeUniqueTempPath(binaryPath);
		const std::filesystem::path tempManifestPath = MakeUniqueTempPath(manifestPath);

		const bool binaryWritten = utils::WriteFileBinary(tempBinaryPath, std::span(
			static_cast<const std::byte*>(record.m_Binary.Data()), record.m_Binary.SizeInBytes()));
		if (!binaryWritten)
		{
			RemoveFileBestEffort(tempBinaryPath);
			return false;
		}

		// Validate the complete result before publication: the manifest digest
		// must equal SHA-256 of the exact bytes about to be published.
		const std::optional<BinaryContentDigest> publishedDigest =
			ComputeFileContentDigest(tempBinaryPath);
		if (!publishedDigest.has_value() ||
			publishedDigest->m_Digest != record.m_Manifest.m_BinaryContentDigest.m_Digest)
		{
			RemoveFileBestEffort(tempBinaryPath);
			return false;
		}

		if (!WriteShaderArtifactCacheRecord(tempManifestPath, record))
		{
			RemoveFileBestEffort(tempBinaryPath);
			RemoveFileBestEffort(tempManifestPath);
			return false;
		}

		// Publish the immutable binary first; the manifest is the commit record.
		if (!PublishFile(tempBinaryPath, binaryPath))
		{
			// A concurrent winner may already have committed the entry.
			std::error_code errorCode;
			if (std::filesystem::exists(manifestPath, errorCode))
			{
				RemoveFileBestEffort(tempBinaryPath);
				RemoveFileBestEffort(tempManifestPath);
				return true;
			}
			// Orphaned binary (no commit record): recover by removing it and
			// retrying once; derived data is safe to discard.
			RemoveFileBestEffort(binaryPath);
			if (!PublishFile(tempBinaryPath, binaryPath))
			{
				RemoveFileBestEffort(tempBinaryPath);
				RemoveFileBestEffort(tempManifestPath);
				return false;
			}
		}

		if (!PublishFile(tempManifestPath, manifestPath))
		{
			// A concurrent winner committed the manifest between our two
			// publishes; its entry is equivalent, so discard our temporary
			// manifest and let the caller load the committed entry.
			RemoveFileBestEffort(tempBinaryPath);
			RemoveFileBestEffort(tempManifestPath);
			return true;
		}
		return true;
	}
}
