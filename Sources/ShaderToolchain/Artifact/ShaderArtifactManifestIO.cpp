#include "Artifact/ShaderArtifactManifestIO.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
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
#include <variant>
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

		class JsonWriter final
		{
		public:
			[[nodiscard]] std::string Finish() && { return std::move(m_Content); }

			void BeginObject()
			{
				AppendCommaIfNeeded();
				Append('{');
				m_PendingValue = false;
			}
			void EndObject()
			{
				Append('}');
				m_PendingValue = true;
			}
			void BeginArray()
			{
				AppendCommaIfNeeded();
				Append('[');
				m_PendingValue = false;
			}
			void EndArray()
			{
				Append(']');
				m_PendingValue = true;
			}
			void Key(std::string_view key)
			{
				AppendCommaIfNeeded();
				WriteEscaped(key);
				Append(':');
				m_PendingValue = false;
			}
			void WriteString(std::string_view value)
			{
				AppendCommaIfNeeded();
				WriteEscaped(value);
				m_PendingValue = true;
			}
			void WriteWideString(std::wstring_view value)
			{
				WriteString(utils::ToString(value));
			}
			void WriteUInt(uint64_t value)
			{
				AppendCommaIfNeeded();
				m_Content += std::to_string(value);
				m_PendingValue = true;
			}
			void WriteInt(int64_t value)
			{
				AppendCommaIfNeeded();
				m_Content += std::to_string(value);
				m_PendingValue = true;
			}

		private:
			void Append(char value) { m_Content += value; }

			void AppendCommaIfNeeded()
			{
				if (m_PendingValue)
				{
					Append(',');
				}
			}

			void WriteEscaped(std::string_view value)
			{
				Append('"');
				for (char current : value)
				{
					switch (current)
					{
					case '"':
						m_Content += "\\\"";
						break;
					case '\\':
						m_Content += "\\\\";
						break;
					case '\b':
						m_Content += "\\b";
						break;
					case '\f':
						m_Content += "\\f";
						break;
					case '\n':
						m_Content += "\\n";
						break;
					case '\r':
						m_Content += "\\r";
						break;
					case '\t':
						m_Content += "\\t";
						break;
					default:
						if (static_cast<unsigned char>(current) < 0x20)
						{
							constexpr char HexChars[] = "0123456789abcdef";
							const unsigned char byte = static_cast<unsigned char>(current);
							m_Content += "\\u00";
							m_Content += HexChars[(byte >> 4) & 0xF];
							m_Content += HexChars[byte & 0xF];
						}
						else
						{
							m_Content += current;
						}
						break;
					}
				}
				Append('"');
			}

			std::string m_Content;
			bool m_PendingValue = false;
		};

		struct JsonValue;
		using JsonArray = std::vector<JsonValue>;
		using JsonObject = std::vector<std::pair<std::string, JsonValue>>;
		struct JsonValue
		{
			std::variant<std::monostate, bool, int64_t, std::string, JsonArray, JsonObject> m_Value;

			[[nodiscard]] bool IsString() const noexcept
			{
				return std::holds_alternative<std::string>(m_Value);
			}
			[[nodiscard]] bool IsInteger() const noexcept
			{
				return std::holds_alternative<int64_t>(m_Value);
			}
			[[nodiscard]] bool IsArray() const noexcept
			{
				return std::holds_alternative<JsonArray>(m_Value);
			}
			[[nodiscard]] bool IsObject() const noexcept
			{
				return std::holds_alternative<JsonObject>(m_Value);
			}
			[[nodiscard]] const std::string& AsString() const noexcept
			{
				return std::get<std::string>(m_Value);
			}
			[[nodiscard]] int64_t AsInteger() const noexcept
			{
				return std::get<int64_t>(m_Value);
			}
			[[nodiscard]] const JsonArray& AsArray() const noexcept
			{
				return std::get<JsonArray>(m_Value);
			}
			[[nodiscard]] const JsonObject& AsObject() const noexcept
			{
				return std::get<JsonObject>(m_Value);
			}
		};

		class JsonParser final
		{
		public:
			explicit JsonParser(std::string_view text) noexcept : m_Text(text) {}

			[[nodiscard]] std::optional<JsonValue> ParseDocument() noexcept
			{
				SkipWhitespace();
				std::optional<JsonValue> value = ParseValue();
				if (!value.has_value())
				{
					return std::nullopt;
				}
				SkipWhitespace();
				if (m_Position != m_Text.size())
				{
					return std::nullopt;
				}
				return value;
			}

		private:
			[[nodiscard]] bool AtEnd() const noexcept { return m_Position >= m_Text.size(); }

			void SkipWhitespace() noexcept
			{
				while (!AtEnd() && (m_Text[m_Position] == ' ' || m_Text[m_Position] == '\t' ||
					m_Text[m_Position] == '\n' || m_Text[m_Position] == '\r'))
				{
					++m_Position;
				}
			}

			[[nodiscard]] std::optional<JsonValue> ParseValue() noexcept
			{
				if (AtEnd())
				{
					return std::nullopt;
				}
				switch (m_Text[m_Position])
				{
				case '{':
					return ParseObject();
				case '[':
					return ParseArray();
				case '"':
					return ParseString();
				default:
					return ParseInteger();
				}
			}

			[[nodiscard]] std::optional<JsonValue> ParseObject() noexcept
			{
				++m_Position; // '{'
				JsonObject object;
				SkipWhitespace();
				if (!AtEnd() && m_Text[m_Position] == '}')
				{
					++m_Position;
					return JsonValue{ std::move(object) };
				}
				while (true)
				{
					SkipWhitespace();
					if (AtEnd() || m_Text[m_Position] != '"')
					{
						return std::nullopt;
					}
					std::optional<JsonValue> key = ParseString();
					if (!key.has_value())
					{
						return std::nullopt;
					}
					for (const auto& existing : object)
					{
						if (existing.first == key->AsString())
						{
							return std::nullopt; // duplicate keys rejected
						}
					}
					SkipWhitespace();
					if (AtEnd() || m_Text[m_Position] != ':')
					{
						return std::nullopt;
					}
					++m_Position;
					SkipWhitespace();
					std::optional<JsonValue> value = ParseValue();
					if (!value.has_value())
					{
						return std::nullopt;
					}
					object.emplace_back(key->AsString(), std::move(*value));
					SkipWhitespace();
					if (AtEnd())
					{
						return std::nullopt;
					}
					if (m_Text[m_Position] == ',')
					{
						++m_Position;
						continue;
					}
					if (m_Text[m_Position] == '}')
					{
						++m_Position;
						return JsonValue{ std::move(object) };
					}
					return std::nullopt;
				}
			}

			[[nodiscard]] std::optional<JsonValue> ParseArray() noexcept
			{
				++m_Position; // '['
				JsonArray array;
				SkipWhitespace();
				if (!AtEnd() && m_Text[m_Position] == ']')
				{
					++m_Position;
					return JsonValue{ std::move(array) };
				}
				while (true)
				{
					SkipWhitespace();
					std::optional<JsonValue> value = ParseValue();
					if (!value.has_value())
					{
						return std::nullopt;
					}
					array.push_back(std::move(*value));
					SkipWhitespace();
					if (AtEnd())
					{
						return std::nullopt;
					}
					if (m_Text[m_Position] == ',')
					{
						++m_Position;
						continue;
					}
					if (m_Text[m_Position] == ']')
					{
						++m_Position;
						return JsonValue{ std::move(array) };
					}
					return std::nullopt;
				}
			}

			[[nodiscard]] std::optional<JsonValue> ParseString() noexcept
			{
				++m_Position; // '"'
				std::string value;
				while (true)
				{
					if (AtEnd())
					{
						return std::nullopt;
					}
					const char current = m_Text[m_Position++];
					if (current == '"')
					{
						return JsonValue{ std::move(value) };
					}
					if (current == '\\')
					{
						if (AtEnd())
						{
							return std::nullopt;
						}
						const char escaped = m_Text[m_Position++];
						switch (escaped)
						{
						case '"':
							value += '"';
							break;
						case '\\':
							value += '\\';
							break;
						case '/':
							value += '/';
							break;
						case 'b':
							value += '\b';
							break;
						case 'f':
							value += '\f';
							break;
						case 'n':
							value += '\n';
							break;
						case 'r':
							value += '\r';
							break;
						case 't':
							value += '\t';
							break;
						case 'u':
						{
							if (m_Position + 4 > m_Text.size())
							{
								return std::nullopt;
							}
							int codeUnit = 0;
							for (int index = 0; index < 4; ++index)
							{
								const int digit = HexDigitValue(m_Text[m_Position + index]);
								if (digit < 0)
								{
									return std::nullopt;
								}
								codeUnit = (codeUnit << 4) | digit;
							}
							m_Position += 4;
							if (codeUnit < 0x80)
							{
								value += static_cast<char>(codeUnit);
							}
							else if (codeUnit < 0x800)
							{
								value += static_cast<char>(0xC0 | (codeUnit >> 6));
								value += static_cast<char>(0x80 | (codeUnit & 0x3F));
							}
							else
							{
								value += static_cast<char>(0xE0 | (codeUnit >> 12));
								value += static_cast<char>(0x80 | ((codeUnit >> 6) & 0x3F));
								value += static_cast<char>(0x80 | (codeUnit & 0x3F));
							}
							break;
						}
						default:
							return std::nullopt;
						}
					}
					else
					{
						value += current;
					}
				}
			}

			[[nodiscard]] std::optional<JsonValue> ParseInteger() noexcept
			{
				bool negative = false;
				if (!AtEnd() && m_Text[m_Position] == '-')
				{
					negative = true;
					++m_Position;
				}
				if (AtEnd() || m_Text[m_Position] < '0' || m_Text[m_Position] > '9')
				{
					return std::nullopt;
				}
				int64_t value = 0;
				while (!AtEnd() && m_Text[m_Position] >= '0' && m_Text[m_Position] <= '9')
				{
					const int digit = m_Text[m_Position] - '0';
					if (value > (INT64_MAX - digit) / 10)
					{
						return std::nullopt;
					}
					value = value * 10 + digit;
					++m_Position;
				}
				return JsonValue{ negative ? -value : value };
			}

			std::string_view m_Text;
			std::size_t m_Position = 0;
		};

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

	bool WriteShaderArtifactManifest(const std::filesystem::path& manifestPath,
		const ShaderArtifactManifest& manifest) noexcept
	{
		const bool created = utils::CreateParentDirectoryIfNotExist(manifestPath);
		if (!created)
		{
			return false;
		}

		JsonWriter writer;
		writer.BeginObject();
		writer.Key("schemaVersion");
		writer.WriteUInt(manifest.m_SchemaVersion);
		writer.Key("recipeHashSchema");
		writer.WriteUInt(manifest.m_RecipeHashSchema);
		writer.Key("recipeId");
		writer.WriteString(Sha256DigestToHex(manifest.m_RecipeId.m_DurableDigest));
		writer.Key("buildKey");
		writer.WriteString(Sha256DigestToHex(manifest.m_BuildKey.m_DurableDigest));
		writer.Key("compiler");
		writer.BeginObject();
		writer.Key("kind");
		writer.WriteString("dxc");
		writer.Key("identity");
		writer.WriteWideString(manifest.m_CompilerIdentity.m_CanonicalIdentity);
		writer.EndObject();
		writer.Key("binaryFormat");
		writer.WriteString(ShaderBinaryFormatText(manifest.m_BinaryFormat));
		writer.Key("spirvTargetEnvironment");
		writer.WriteString(SpirVTargetEnvironmentText(manifest.m_SpirVTargetEnvironment));
		writer.Key("bindingAbiRevision");
		writer.WriteUInt(manifest.m_BindingABIRevision);
		writer.Key("coordinateOptions");
		writer.WriteUInt(static_cast<uint32_t>(manifest.m_CoordinateOptions));
		writer.Key("stage");
		writer.WriteString(ShaderStageText(manifest.m_Stage));
		writer.Key("logicalSource");
		writer.WriteWideString(manifest.m_LogicalSourcePath.generic_wstring());
		writer.Key("physicalSource");
		writer.WriteString(utils::Canonical(manifest.m_SourcePath).string());
		writer.Key("entryPoint");
		writer.WriteWideString(manifest.m_EntryPoint);
		writer.Key("target");
		writer.WriteWideString(manifest.m_TargetString);
		writer.Key("defines");
		writer.BeginArray();
		for (const std::wstring& define : manifest.m_Defines)
		{
			writer.WriteWideString(define);
		}
		writer.EndArray();
		writer.Key("includeDirs");
		writer.BeginArray();
		for (const std::filesystem::path& includeDir : manifest.m_IncludeDirs)
		{
			writer.WriteString(utils::Canonical(includeDir).string());
		}
		writer.EndArray();
		writer.Key("extraArgs");
		writer.BeginArray();
		for (const std::wstring& extraArg : manifest.m_ExtraArgs)
		{
			writer.WriteWideString(extraArg);
		}
		writer.EndArray();
		writer.Key("dependencies");
		writer.BeginArray();
		for (const ShaderArtifactDependency& dependency : manifest.m_Dependencies)
		{
			writer.BeginObject();
			writer.Key("path");
			writer.WriteString(utils::Canonical(dependency.m_Path).string());
			writer.Key("lastWriteTimeTicks");
			writer.WriteInt(dependency.m_LastWriteTimeTicks);
			writer.EndObject();
		}
		writer.EndArray();
		writer.Key("binaryContentDigest");
		writer.WriteString(Sha256DigestToHex(manifest.m_BinaryContentDigest.m_Digest));
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
		struct ManifestJsonMapper final
		{
			[[nodiscard]] std::optional<ShaderArtifactManifest> Map(const JsonValue& root) noexcept
			{
				if (!root.IsObject())
				{
					return std::nullopt;
				}
				ShaderArtifactManifest manifest{};
				uint32_t seenMask = 0;
				constexpr uint32_t SchemaBit = 1u << 0;
				constexpr uint32_t RecipeHashSchemaBit = 1u << 1;
				constexpr uint32_t RecipeIdBit = 1u << 2;
				constexpr uint32_t BuildKeyBit = 1u << 3;
				constexpr uint32_t CompilerBit = 1u << 4;
				constexpr uint32_t BinaryFormatBit = 1u << 5;
				constexpr uint32_t EnvironmentBit = 1u << 6;
				constexpr uint32_t AbiBit = 1u << 7;
				constexpr uint32_t CoordinatesBit = 1u << 8;
				constexpr uint32_t StageBit = 1u << 9;
				constexpr uint32_t LogicalSourceBit = 1u << 10;
				constexpr uint32_t PhysicalSourceBit = 1u << 11;
				constexpr uint32_t EntryBit = 1u << 12;
				constexpr uint32_t TargetBit = 1u << 13;
				constexpr uint32_t DefinesBit = 1u << 14;
				constexpr uint32_t IncludeDirsBit = 1u << 15;
				constexpr uint32_t ExtraArgsBit = 1u << 16;
				constexpr uint32_t DependenciesBit = 1u << 17;
				constexpr uint32_t DigestBit = 1u << 18;
				constexpr uint32_t RequiredMask = SchemaBit | RecipeHashSchemaBit | RecipeIdBit |
					BuildKeyBit | CompilerBit | BinaryFormatBit | EnvironmentBit | AbiBit |
					CoordinatesBit | StageBit | LogicalSourceBit | PhysicalSourceBit |
					EntryBit | TargetBit | DefinesBit | IncludeDirsBit | ExtraArgsBit |
					DependenciesBit | DigestBit;

				for (const auto& [key, value] : root.AsObject())
				{
					if (key == "schemaVersion")
					{
						if (!value.IsInteger() ||
							value.AsInteger() != ShaderArtifactManifestSchemaVersion)
						{
							return std::nullopt;
						}
						manifest.m_SchemaVersion = static_cast<uint32_t>(value.AsInteger());
						seenMask |= SchemaBit;
					}
					else if (key == "recipeHashSchema")
					{
						if (!value.IsInteger() || value.AsInteger() != ShaderRecipeHashSchema)
						{
							return std::nullopt;
						}
						manifest.m_RecipeHashSchema = static_cast<uint32_t>(value.AsInteger());
						seenMask |= RecipeHashSchemaBit;
					}
					else if (key == "recipeId")
					{
						if (!value.IsString())
						{
							return std::nullopt;
						}
						const auto parsed = ParseHexSha256Digest(value.AsString());
						if (!parsed.has_value())
						{
							return std::nullopt;
						}
						manifest.m_RecipeId.m_DurableDigest = *parsed;
						seenMask |= RecipeIdBit;
					}
					else if (key == "buildKey")
					{
						if (!value.IsString())
						{
							return std::nullopt;
						}
						const auto parsed = ParseHexSha256Digest(value.AsString());
						if (!parsed.has_value())
						{
							return std::nullopt;
						}
						manifest.m_BuildKey.m_DurableDigest = *parsed;
						seenMask |= BuildKeyBit;
					}
					else if (key == "compiler")
					{
						if (!value.IsObject())
						{
							return std::nullopt;
						}
						bool hasKind = false;
						bool hasIdentity = false;
						for (const auto& [compilerKey, compilerValue] : value.AsObject())
						{
							if (compilerKey == "kind")
							{
								if (!compilerValue.IsString() || compilerValue.AsString() != "dxc")
								{
									return std::nullopt;
								}
								hasKind = true;
							}
							else if (compilerKey == "identity")
							{
								if (!compilerValue.IsString())
								{
									return std::nullopt;
								}
								manifest.m_CompilerIdentity.m_CanonicalIdentity =
									utils::ToWideString(compilerValue.AsString());
								hasIdentity = true;
							}
							else
							{
								return std::nullopt;
							}
						}
						if (!hasKind || !hasIdentity)
						{
							return std::nullopt;
						}
						seenMask |= CompilerBit;
					}
					else if (key == "binaryFormat")
					{
						if (!value.IsString() ||
							!ParseShaderBinaryFormat(value.AsString(), manifest.m_BinaryFormat))
						{
							return std::nullopt;
						}
						seenMask |= BinaryFormatBit;
					}
					else if (key == "spirvTargetEnvironment")
					{
						if (!value.IsString() ||
							!ParseSpirVTargetEnvironment(
								value.AsString(), manifest.m_SpirVTargetEnvironment))
						{
							return std::nullopt;
						}
						seenMask |= EnvironmentBit;
					}
					else if (key == "bindingAbiRevision")
					{
						if (!value.IsInteger() || value.AsInteger() < 0 ||
							value.AsInteger() > UINT32_MAX)
						{
							return std::nullopt;
						}
						manifest.m_BindingABIRevision = static_cast<uint32_t>(value.AsInteger());
						seenMask |= AbiBit;
					}
					else if (key == "coordinateOptions")
					{
						if (!value.IsInteger() || value.AsInteger() < 0 ||
							value.AsInteger() > UINT32_MAX)
						{
							return std::nullopt;
						}
						manifest.m_CoordinateOptions =
							static_cast<ShaderCoordinateOptions>(value.AsInteger());
						seenMask |= CoordinatesBit;
					}
					else if (key == "stage")
					{
						if (!value.IsString() ||
							!ParseShaderStage(value.AsString(), manifest.m_Stage))
						{
							return std::nullopt;
						}
						seenMask |= StageBit;
					}
					else if (key == "logicalSource")
					{
						if (!value.IsString())
						{
							return std::nullopt;
						}
						manifest.m_LogicalSourcePath = utils::ToWideString(value.AsString());
						seenMask |= LogicalSourceBit;
					}
					else if (key == "physicalSource")
					{
						if (!value.IsString())
						{
							return std::nullopt;
						}
						manifest.m_SourcePath = utils::ToWideString(value.AsString());
						seenMask |= PhysicalSourceBit;
					}
					else if (key == "entryPoint")
					{
						if (!value.IsString())
						{
							return std::nullopt;
						}
						manifest.m_EntryPoint = utils::ToWideString(value.AsString());
						seenMask |= EntryBit;
					}
					else if (key == "target")
					{
						if (!value.IsString())
						{
							return std::nullopt;
						}
						manifest.m_TargetString = utils::ToWideString(value.AsString());
						seenMask |= TargetBit;
					}
					else if (key == "defines")
					{
						if (!value.IsArray())
						{
							return std::nullopt;
						}
						for (const JsonValue& define : value.AsArray())
						{
							if (!define.IsString())
							{
								return std::nullopt;
							}
							manifest.m_Defines.push_back(utils::ToWideString(define.AsString()));
						}
						seenMask |= DefinesBit;
					}
					else if (key == "includeDirs")
					{
						if (!value.IsArray())
						{
							return std::nullopt;
						}
						for (const JsonValue& includeDir : value.AsArray())
						{
							if (!includeDir.IsString())
							{
								return std::nullopt;
							}
							manifest.m_IncludeDirs.emplace_back(
								utils::ToWideString(includeDir.AsString()));
						}
						seenMask |= IncludeDirsBit;
					}
					else if (key == "extraArgs")
					{
						if (!value.IsArray())
						{
							return std::nullopt;
						}
						for (const JsonValue& extraArg : value.AsArray())
						{
							if (!extraArg.IsString())
							{
								return std::nullopt;
							}
							manifest.m_ExtraArgs.push_back(utils::ToWideString(extraArg.AsString()));
						}
						seenMask |= ExtraArgsBit;
					}
					else if (key == "dependencies")
					{
						if (!value.IsArray())
						{
							return std::nullopt;
						}
						for (const JsonValue& dependency : value.AsArray())
						{
							if (!dependency.IsObject())
							{
								return std::nullopt;
							}
							ShaderArtifactDependency record{};
							bool hasPath = false;
							bool hasTicks = false;
							for (const auto& [dependencyKey, dependencyValue] : dependency.AsObject())
							{
								if (dependencyKey == "path")
								{
									if (!dependencyValue.IsString())
									{
										return std::nullopt;
									}
									record.m_Path = utils::ToWideString(dependencyValue.AsString());
									hasPath = true;
								}
								else if (dependencyKey == "lastWriteTimeTicks")
								{
									if (!dependencyValue.IsInteger())
									{
										return std::nullopt;
									}
									record.m_LastWriteTimeTicks = dependencyValue.AsInteger();
									hasTicks = true;
								}
								else
								{
									return std::nullopt;
								}
							}
							if (!hasPath || !hasTicks)
							{
								return std::nullopt;
							}
							manifest.m_Dependencies.push_back(std::move(record));
						}
						seenMask |= DependenciesBit;
					}
					else if (key == "binaryContentDigest")
					{
						if (!value.IsString())
						{
							return std::nullopt;
						}
						const auto parsed = ParseHexSha256Digest(value.AsString());
						if (!parsed.has_value())
						{
							return std::nullopt;
						}
						manifest.m_BinaryContentDigest.m_Digest = *parsed;
						seenMask |= DigestBit;
					}
					else
					{
						// Unknown keys are rejected: schema evolution requires a bump.
						return std::nullopt;
					}
				}

				if (seenMask != RequiredMask)
				{
					return std::nullopt;
				}
				return manifest;
			}
		};
	}

	std::optional<ShaderArtifactManifest> ReadShaderArtifactManifest(
		const std::filesystem::path& manifestPath) noexcept
	{
		std::ifstream in(manifestPath, std::ios::binary);
		if (!in)
		{
			return std::nullopt;
		}
		const std::string content((std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());

		JsonParser parser(content);
		const std::optional<JsonValue> document = parser.ParseDocument();
		if (!document.has_value())
		{
			return std::nullopt;
		}
		return ManifestJsonMapper{}.Map(*document);
	}

	std::optional<ShaderArtifact> LoadShaderArtifact(const std::filesystem::path& manifestPath,
		const std::filesystem::path& binaryPath) noexcept
	{
		std::error_code errorCode;
		if (!std::filesystem::exists(manifestPath, errorCode) ||
			!std::filesystem::exists(binaryPath, errorCode))
		{
			return std::nullopt;
		}

		const std::optional<ShaderArtifactManifest> manifest =
			ReadShaderArtifactManifest(manifestPath);
		if (!manifest.has_value())
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
			actualDigest->m_Digest != manifest->m_BinaryContentDigest.m_Digest)
		{
			return std::nullopt;
		}

		ShaderArtifact artifact{};
		artifact.m_Manifest = *manifest;
		artifact.m_Binary = *binary;
		return artifact;
	}

	bool PublishShaderArtifact(const std::filesystem::path& binaryPath,
		const std::filesystem::path& manifestPath, const ShaderArtifact& artifact) noexcept
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
			static_cast<const std::byte*>(artifact.m_Binary.Data()), artifact.m_Binary.SizeInBytes()));
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
			publishedDigest->m_Digest != artifact.m_Manifest.m_BinaryContentDigest.m_Digest)
		{
			RemoveFileBestEffort(tempBinaryPath);
			return false;
		}

		if (!WriteShaderArtifactManifest(tempManifestPath, artifact.m_Manifest))
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
