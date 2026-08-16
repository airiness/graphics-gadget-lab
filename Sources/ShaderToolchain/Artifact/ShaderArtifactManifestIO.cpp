#include "Artifact/ShaderArtifactManifestIO.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Json/JsonValue.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"

#include <process.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
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

	bool WriteShaderArtifactCacheRecord(const std::filesystem::path& manifestPath,
		const ShaderArtifactCacheRecord& record) noexcept
	{
		const bool created = utils::CreateParentDirectoryIfNotExist(manifestPath);
		if (!created)
		{
			return false;
		}

		const ShaderArtifactManifest& manifest = record.m_Manifest;
		json::JsonWriter writer;
		writer.BeginObject();

		writer.Key("manifest");
		writer.BeginObject();
		writer.Key("schemaVersion");
		writer.WriteInteger(manifest.m_SchemaVersion);
		writer.Key("recipeHashSchema");
		writer.WriteInteger(manifest.m_RecipeHashSchema);
		writer.Key("recipeId");
		writer.WriteString(Sha256DigestToHex(manifest.m_RecipeId.m_DurableDigest));
		writer.Key("buildKey");
		writer.WriteString(Sha256DigestToHex(manifest.m_BuildKey.m_DurableDigest));
		writer.Key("compiler");
		writer.BeginObject();
		writer.Key("kind");
		writer.WriteString("dxc");
		writer.Key("identity");
		writer.WriteString(utils::ToString(manifest.m_CompilerIdentity.m_CanonicalIdentity));
		writer.EndObject();
		writer.Key("targetProfile");
		writer.WriteString(ShaderTargetProfileText(manifest.m_TargetProfile));
		writer.Key("binaryFormat");
		writer.WriteString(ShaderBinaryFormatText(manifest.m_BinaryFormat));
		writer.Key("spirvTargetEnvironment");
		writer.WriteString(SpirVTargetEnvironmentText(manifest.m_SpirVTargetEnvironment));
		writer.Key("bindingAbiRevision");
		writer.WriteInteger(manifest.m_BindingABIRevision);
		writer.Key("coordinateOptions");
		writer.WriteInteger(static_cast<uint32_t>(manifest.m_CoordinateOptions));
		writer.Key("stage");
		writer.WriteString(ShaderStageText(manifest.m_Stage));
		writer.Key("shaderModel");
		writer.WriteString(ShaderModelText(manifest.m_ShaderModel));
		writer.Key("hlslVersion");
		writer.WriteString(utils::ToString(manifest.m_HlslVersion));
		writer.Key("compileFlags");
		writer.WriteInteger(static_cast<uint32_t>(manifest.m_CompileFlags));
		writer.Key("optimizationLevel");
		writer.WriteString(utils::ToString(manifest.m_OptimizationLevel));
		writer.Key("logicalSource");
		writer.WriteString(utils::ToString(manifest.m_LogicalSourcePath.generic_wstring()));
		writer.Key("entryPoint");
		writer.WriteString(utils::ToString(manifest.m_EntryPoint));
		writer.Key("target");
		writer.WriteString(utils::ToString(manifest.m_TargetString));
		writer.Key("defines");
		writer.BeginArray();
		for (const std::wstring& define : manifest.m_Defines)
		{
			writer.WriteString(utils::ToString(define));
		}
		writer.EndArray();
		writer.Key("logicalIncludeDirs");
		writer.BeginArray();
		for (const std::filesystem::path& includeDir : manifest.m_LogicalIncludeDirs)
		{
			writer.WriteString(utils::ToString(includeDir.generic_wstring()));
		}
		writer.EndArray();
		writer.Key("extraArgs");
		writer.BeginArray();
		for (const std::wstring& extraArg : manifest.m_ExtraArgs)
		{
			writer.WriteString(utils::ToString(extraArg));
		}
		writer.EndArray();
		writer.Key("binaryContentDigest");
		writer.WriteString(Sha256DigestToHex(manifest.m_BinaryContentDigest.m_Digest));
		writer.EndObject();

		writer.Key("local");
		writer.BeginObject();
		writer.Key("physicalSource");
		writer.WriteString(utils::Canonical(record.m_PhysicalSourcePath).string());
		writer.Key("physicalIncludeDirs");
		writer.BeginArray();
		for (const std::filesystem::path& includeDir : record.m_PhysicalIncludeDirs)
		{
			writer.WriteString(utils::Canonical(includeDir).string());
		}
		writer.EndArray();
		writer.Key("dependencies");
		writer.BeginArray();
		for (const ShaderArtifactDependency& dependency : record.m_Dependencies)
		{
			writer.BeginObject();
			writer.Key("logicalPath");
			writer.WriteString(utils::ToString(dependency.m_LogicalPath.generic_wstring()));
			writer.Key("physicalPath");
			writer.WriteString(utils::Canonical(dependency.m_PhysicalPath).string());
			writer.Key("contentDigest");
			writer.WriteString(Sha256DigestToHex(dependency.m_ContentDigest));
			writer.Key("lastWriteTimeTicks");
			writer.WriteInteger(dependency.m_LastWriteTimeTicks);
			writer.EndObject();
		}
		writer.EndArray();
		writer.EndObject();

		writer.EndObject();

		std::ofstream out(manifestPath, std::ios::binary);
		if (!out)
		{
			return false;
		}
		const std::string content = std::move(writer).Finish();
		out.write(content.data(), static_cast<std::streamsize>(content.size()));
		return static_cast<bool>(out);
	}

	namespace
	{
		[[nodiscard]] const json::JsonValue* FindField(
			const json::JsonObject& object, std::string_view key) noexcept
		{
			for (const json::JsonMember& member : object)
			{
				if (member.first == key)
				{
					return &member.second;
				}
			}
			return nullptr;
		}

		[[nodiscard]] bool ParseRequiredInteger(const json::JsonObject& object,
			std::string_view key, int64_t& outValue) noexcept
		{
			const json::JsonValue* field = FindField(object, key);
			if (field == nullptr || !field->IsInteger())
			{
				return false;
			}
			outValue = field->AsInteger();
			return true;
		}

		[[nodiscard]] bool ParseRequiredString(const json::JsonObject& object,
			std::string_view key, std::string& outValue) noexcept
		{
			const json::JsonValue* field = FindField(object, key);
			if (field == nullptr || !field->IsString())
			{
				return false;
			}
			outValue = field->AsString();
			return true;
		}

		[[nodiscard]] bool ParseRequiredStringArray(const json::JsonObject& object,
			std::string_view key, std::vector<std::string>& outValues) noexcept
		{
			const json::JsonValue* field = FindField(object, key);
			if (field == nullptr || !field->IsArray())
			{
				return false;
			}
			for (const json::JsonValue& element : field->AsArray())
			{
				if (!element.IsString())
				{
					return false;
				}
				outValues.push_back(element.AsString());
			}
			return true;
		}

		struct CacheRecordJsonMapper final
		{
			[[nodiscard]] std::optional<ShaderArtifactCacheRecord> Map(
				const json::JsonValue& root) noexcept
			{
				if (!root.IsObject())
				{
					return std::nullopt;
				}
				const json::JsonObject& rootObject = root.AsObject();

				const json::JsonValue* manifestValue = FindField(rootObject, "manifest");
				const json::JsonValue* localValue = FindField(rootObject, "local");
				if (manifestValue == nullptr || !manifestValue->IsObject() ||
					localValue == nullptr || !localValue->IsObject())
				{
					return std::nullopt;
				}
				if (rootObject.size() != 2)
				{
					return std::nullopt; // unknown keys rejected: schema bump required
				}

				ShaderArtifactCacheRecord record{};
				if (!MapManifest(manifestValue->AsObject(), record.m_Manifest) ||
					!MapLocal(localValue->AsObject(), record))
				{
					return std::nullopt;
				}
				return record;
			}

		private:
			[[nodiscard]] bool MapManifest(const json::JsonObject& object,
				ShaderArtifactManifest& manifest) noexcept
			{
				std::string text;
				int64_t integer = 0;
				std::vector<std::string> strings;
				uint32_t seenMask = 0;
				constexpr uint32_t AllBits = (1u << 22) - 1;

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

				const json::JsonValue* compiler = FindField(object, "compiler");
				if (compiler == nullptr || !compiler->IsObject())
				{
					return false;
				}
				bool hasKind = false;
				bool hasIdentity = false;
				for (const json::JsonMember& member : compiler->AsObject())
				{
					if (member.first == "kind")
					{
						if (!member.second.IsString() || member.second.AsString() != "dxc")
						{
							return false;
						}
						hasKind = true;
					}
					else if (member.first == "identity")
					{
						if (!member.second.IsString())
						{
							return false;
						}
						manifest.m_CompilerIdentity.m_CanonicalIdentity =
							utils::ToWideString(member.second.AsString());
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

				return seenMask == AllBits && object.size() == 22;
			}

			[[nodiscard]] bool MapLocal(const json::JsonObject& object,
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

				const json::JsonValue* dependencies = FindField(object, "dependencies");
				if (dependencies == nullptr || !dependencies->IsArray())
				{
					return false;
				}
				for (const json::JsonValue& dependencyValue : dependencies->AsArray())
				{
					if (!dependencyValue.IsObject())
					{
						return false;
					}
					const json::JsonObject& dependencyObject = dependencyValue.AsObject();
					ShaderArtifactDependency dependency{};
					std::string logicalPath;
					std::string physicalPath;
					std::string contentDigest;
					int64_t ticks = 0;
					if (!ParseRequiredString(dependencyObject, "logicalPath", logicalPath) ||
						!ParseRequiredString(dependencyObject, "physicalPath", physicalPath) ||
						!ParseRequiredString(dependencyObject, "contentDigest", contentDigest) ||
						!ParseRequiredInteger(dependencyObject, "lastWriteTimeTicks", ticks) ||
						dependencyObject.size() != 4)
					{
						return false;
					}
					const auto parsedDigest = ParseHexSha256Digest(contentDigest);
					if (!parsedDigest.has_value())
					{
						return false;
					}
					dependency.m_LogicalPath = utils::ToWideString(logicalPath);
					dependency.m_PhysicalPath = utils::ToWideString(physicalPath);
					dependency.m_ContentDigest = *parsedDigest;
					dependency.m_LastWriteTimeTicks = ticks;
					record.m_Dependencies.push_back(std::move(dependency));
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

		const std::optional<json::JsonValue> document = json::ParseJsonDocument(content);
		if (!document.has_value())
		{
			return std::nullopt;
		}
		return CacheRecordJsonMapper{}.Map(*document);
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

		const std::optional<ShaderBinary> binary = ReadFileBinary(binaryPath);
		if (!binary.has_value())
		{
			return std::nullopt;
		}

		const std::optional<BinaryContentDigest> actualDigest =
			ComputeFileContentDigest(binaryPath);
		if (!actualDigest.has_value() ||
			actualDigest->m_Digest != record->m_Manifest.m_BinaryContentDigest.m_Digest)
		{
			return std::nullopt;
		}

		record->m_Binary = *binary;
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
