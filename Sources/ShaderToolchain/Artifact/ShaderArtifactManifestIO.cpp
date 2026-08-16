#include "Artifact/ShaderArtifactManifestIO.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"

#include <process.h>

#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
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

		[[nodiscard]] std::string ToHex128(ShaderHash128 hash)
		{
			return std::format("{:016x}{:016x}", hash.m_HighBits, hash.m_LowBits);
		}

		[[nodiscard]] std::optional<ShaderHash128> ParseHex128(std::string_view hex) noexcept
		{
			if (hex.size() != 32)
			{
				return std::nullopt;
			}
			ShaderHash128 hash{};
			const auto parse = [&hex](std::size_t offset, uint64_t& out) noexcept
				{
					out = 0;
					for (std::size_t index = 0; index < 16; ++index)
					{
						const int digit = HexDigitValue(hex[offset + index]);
						if (digit < 0)
						{
							return false;
						}
						out = (out << 4) | static_cast<uint64_t>(digit);
					}
					return true;
				};
			if (!parse(0, hash.m_HighBits) || !parse(16, hash.m_LowBits))
			{
				return std::nullopt;
			}
			return hash;
		}

		[[nodiscard]] std::vector<std::wstring> SplitList(std::wstring_view text) noexcept
		{
			std::vector<std::wstring> result;
			std::size_t begin = 0;
			while (begin <= text.size())
			{
				const std::size_t separator = text.find(L';', begin);
				const std::size_t end =
					separator == std::wstring_view::npos ? text.size() : separator;
				if (end > begin)
				{
					result.emplace_back(text.substr(begin, end - begin));
				}
				if (separator == std::wstring_view::npos)
				{
					break;
				}
				begin = separator + 1;
			}
			return result;
		}

		[[nodiscard]] std::wstring JoinList(const std::vector<std::wstring>& values)
		{
			std::wstring joined;
			for (std::size_t index = 0; index < values.size(); ++index)
			{
				if (index > 0)
				{
					joined += L";";
				}
				joined += values[index];
			}
			return joined;
		}

		[[nodiscard]] std::optional<int64_t> ParseInt64(std::string_view text) noexcept
		{
			int64_t value = 0;
			const auto [end, error] =
				std::from_chars(text.data(), text.data() + text.size(), value);
			if (error != std::errc{} || end != text.data() + text.size())
			{
				return std::nullopt;
			}
			return value;
		}

		[[nodiscard]] std::optional<uint32_t> ParseUInt32(std::string_view text) noexcept
		{
			uint32_t value = 0;
			const auto [end, error] =
				std::from_chars(text.data(), text.data() + text.size(), value);
			if (error != std::errc{} || end != text.data() + text.size())
			{
				return std::nullopt;
			}
			return value;
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

	bool WriteShaderArtifactManifest(const std::filesystem::path& manifestPath,
		const ShaderArtifactManifest& manifest) noexcept
	{
		const bool created = utils::CreateParentDirectoryIfNotExist(manifestPath);
		if (!created)
		{
			return false;
		}

		std::ofstream out(manifestPath, std::ios::binary);
		if (!out)
		{
			return false;
		}

		out << "schema=" << manifest.m_SchemaVersion << "\n";
		out << "recipe_hash_schema=" << manifest.m_RecipeHashSchema << "\n";
		out << "recipe=" << ToHex128(manifest.m_RecipeId.m_Digest) << "\n";
		out << "build_key=" << ToHex128(manifest.m_BuildKey.m_Digest) << "\n";
		out << "dxc_version=" <<
			utils::ToString(manifest.m_CompilerIdentity.m_CanonicalIdentity) << "\n";
		out << "binary_format=" << ShaderBinaryFormatText(manifest.m_BinaryFormat) << "\n";
		out << "target_environment=" <<
			SpirVTargetEnvironmentText(manifest.m_SpirVTargetEnvironment) << "\n";
		out << "binding_abi_revision=" << manifest.m_BindingABIRevision << "\n";
		out << "coordinate_options=" <<
			static_cast<uint32_t>(manifest.m_CoordinateOptions) << "\n";
		out << "binary_digest=" << Sha256DigestToHex(manifest.m_BinaryContentDigest.m_Digest) << "\n";
		out << "src=" << utils::Canonical(manifest.m_SourcePath).string() << "\n";
		out << "entry=" << utils::ToString(manifest.m_EntryPoint) << "\n";
		out << "stage=" << ShaderStageText(manifest.m_Stage) << "\n";
		out << "target=" << utils::ToString(manifest.m_TargetString) << "\n";
		out << "defines=" << utils::ToString(JoinList(manifest.m_Defines)) << "\n";
		out << "includes=";
		for (std::size_t index = 0; index < manifest.m_IncludeDirs.size(); ++index)
		{
			if (index > 0)
			{
				out << ";";
			}
			out << utils::Canonical(manifest.m_IncludeDirs[index]).string();
		}
		out << "\n";
		out << "extra=" << utils::ToString(JoinList(manifest.m_ExtraArgs)) << "\n";
		for (const ShaderArtifactDependency& dependency : manifest.m_Dependencies)
		{
			out << "dep=" << utils::Canonical(dependency.m_Path).string()
				<< "|mtime=" << dependency.m_LastWriteTimeTicks << "\n";
		}
		return static_cast<bool>(out);
	}

	std::optional<ShaderArtifactManifest> ReadShaderArtifactManifest(
		const std::filesystem::path& manifestPath) noexcept
	{
		std::ifstream in(manifestPath, std::ios::binary);
		if (!in)
		{
			return std::nullopt;
		}

		ShaderArtifactManifest manifest{};
		bool hasSchema = false;
		bool hasRecipeHashSchema = false;
		bool hasRecipe = false;
		bool hasBuildKey = false;
		bool hasDxcVersion = false;
		bool hasBinaryFormat = false;
		bool hasTargetEnvironment = false;
		bool hasBindingABIRevision = false;
		bool hasCoordinateOptions = false;
		bool hasBinaryDigest = false;
		bool hasSource = false;
		bool hasEntry = false;
		bool hasStage = false;
		bool hasTarget = false;

		std::string line;
		while (std::getline(in, line))
		{
			if (line.rfind("dep=", 0) == 0)
			{
				const std::size_t bar = line.find("|mtime=");
				if (bar == std::string::npos || bar <= 4)
				{
					return std::nullopt;
				}
				const std::optional<int64_t> ticks =
					ParseInt64(std::string_view(line).substr(bar + 7));
				if (!ticks.has_value())
				{
					return std::nullopt;
				}
				manifest.m_Dependencies.push_back({
					.m_Path = std::filesystem::path(line.substr(4, bar - 4)),
					.m_LastWriteTimeTicks = *ticks,
				});
				continue;
			}

			const std::size_t separator = line.find('=');
			if (separator == std::string::npos)
			{
				return std::nullopt;
			}
			const std::string_view key(line.data(), separator);
			const std::string_view value = std::string_view(line).substr(separator + 1);

			if (key == "schema")
			{
				const auto parsed = ParseUInt32(value);
				if (!parsed.has_value() || *parsed != ShaderCacheMetadataSchema)
				{
					return std::nullopt;
				}
				manifest.m_SchemaVersion = *parsed;
				hasSchema = true;
			}
			else if (key == "recipe_hash_schema")
			{
				const auto parsed = ParseUInt32(value);
				if (!parsed.has_value() || *parsed != ShaderRecipeHashSchema)
				{
					return std::nullopt;
				}
				manifest.m_RecipeHashSchema = *parsed;
				hasRecipeHashSchema = true;
			}
			else if (key == "recipe")
			{
				const auto parsed = ParseHex128(value);
				if (!parsed.has_value())
				{
					return std::nullopt;
				}
				manifest.m_RecipeId.m_Digest = *parsed;
				hasRecipe = true;
			}
			else if (key == "build_key")
			{
				const auto parsed = ParseHex128(value);
				if (!parsed.has_value())
				{
					return std::nullopt;
				}
				manifest.m_BuildKey.m_Digest = *parsed;
				hasBuildKey = true;
			}
			else if (key == "dxc_version")
			{
				manifest.m_CompilerIdentity.m_CanonicalIdentity = utils::ToWideString(value);
				hasDxcVersion = true;
			}
			else if (key == "binary_format")
			{
				if (!ParseShaderBinaryFormat(value, manifest.m_BinaryFormat))
				{
					return std::nullopt;
				}
				hasBinaryFormat = true;
			}
			else if (key == "target_environment")
			{
				if (!ParseSpirVTargetEnvironment(value, manifest.m_SpirVTargetEnvironment))
				{
					return std::nullopt;
				}
				hasTargetEnvironment = true;
			}
			else if (key == "binding_abi_revision")
			{
				const auto parsed = ParseUInt32(value);
				if (!parsed.has_value())
				{
					return std::nullopt;
				}
				manifest.m_BindingABIRevision = *parsed;
				hasBindingABIRevision = true;
			}
			else if (key == "coordinate_options")
			{
				const auto parsed = ParseUInt32(value);
				if (!parsed.has_value())
				{
					return std::nullopt;
				}
				manifest.m_CoordinateOptions =
					static_cast<ShaderCoordinateOptions>(*parsed);
				hasCoordinateOptions = true;
			}
			else if (key == "binary_digest")
			{
				const auto parsed = ParseHexSha256Digest(value);
				if (!parsed.has_value())
				{
					return std::nullopt;
				}
				manifest.m_BinaryContentDigest.m_Digest = *parsed;
				hasBinaryDigest = true;
			}
			else if (key == "src")
			{
				manifest.m_SourcePath = utils::ToWideString(value);
				hasSource = true;
			}
			else if (key == "entry")
			{
				manifest.m_EntryPoint = utils::ToWideString(value);
				hasEntry = true;
			}
			else if (key == "stage")
			{
				if (!ParseShaderStage(value, manifest.m_Stage))
				{
					return std::nullopt;
				}
				hasStage = true;
			}
			else if (key == "target")
			{
				manifest.m_TargetString = utils::ToWideString(value);
				hasTarget = true;
			}
			else if (key == "defines")
			{
				manifest.m_Defines = SplitList(utils::ToWideString(value));
			}
			else if (key == "includes")
			{
				manifest.m_IncludeDirs.clear();
				for (const std::wstring& dir : SplitList(utils::ToWideString(value)))
				{
					manifest.m_IncludeDirs.emplace_back(dir);
				}
			}
			else if (key == "extra")
			{
				manifest.m_ExtraArgs = SplitList(utils::ToWideString(value));
			}
			else
			{
				// Unknown keys are rejected: schema evolution requires a bump.
				return std::nullopt;
			}
		}

		if (!hasSchema || !hasRecipeHashSchema || !hasRecipe || !hasBuildKey ||
			!hasDxcVersion || !hasBinaryFormat || !hasTargetEnvironment ||
			!hasBindingABIRevision || !hasCoordinateOptions || !hasBinaryDigest ||
			!hasSource || !hasEntry || !hasStage || !hasTarget)
		{
			return std::nullopt;
		}
		return manifest;
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
