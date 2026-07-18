#include "Core/Precompiled.h"
#include "Graphics/IBLBakeCache.h"
#include "Graphics/Shader/ShaderPaths.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/Utility/TextureUtils.h"
#include "Core/Hash/KeyHash.h"
#include "Core/Utility/PathUtils.h"

#include <array>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace gglab
{
	namespace
	{
		constexpr std::string_view CompleteMarker = "gglab-ibl-cache-v1";
		constexpr std::array<std::string_view, 5> BakeShaderPaths = {
			"Passes/PassIBLEnvironment.hlsl",
			"Passes/PassIBLEnvironmentMip.hlsl",
			"Passes/PassIBLIrradiance.hlsl",
			"Passes/PassIBLPrefilteredSpecular.hlsl",
			"Passes/PassIBLBrdfLUT.hlsl",
		};

		void HashShaderDependency(
			uint64_t& hash,
			const std::filesystem::path& shaderRoot,
			const std::filesystem::path& path,
			std::set<std::filesystem::path>& visited) noexcept
		{
			std::error_code errorCode;
			const auto normalized = std::filesystem::weakly_canonical(path, errorCode);
			const auto key = errorCode ? path.lexically_normal() : normalized;
			if (!visited.insert(key).second)
			{
				return;
			}

			std::error_code relativeError;
			const auto canonicalRoot = std::filesystem::weakly_canonical(shaderRoot, relativeError);
			const auto relativePath = relativeError ? key : std::filesystem::relative(key, canonicalRoot, relativeError);
			const std::string pathText = (relativeError ? key : relativePath).generic_string();
			FNV1a64::MixValue(hash, std::string_view(pathText));
			std::ifstream stream(key, std::ios::binary);
			if (!stream)
			{
				FNV1a64::MixValue(hash, std::string_view("missing-shader-dependency"));
				return;
			}

			std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			FNV1a64::MixBytes(hash, source.data(), source.size());
			size_t cursor = 0;
			while ((cursor = source.find("#include", cursor)) != std::string::npos)
			{
				const size_t delimiter = source.find_first_of("\"<", cursor + 8);
				if (delimiter == std::string::npos)
				{
					break;
				}
				const char closing = source[delimiter] == '<' ? '>' : '\"';
				const size_t end = source.find(closing, delimiter + 1);
				if (end == std::string::npos)
				{
					break;
				}
				const auto include = std::filesystem::path(source.substr(delimiter + 1, end - delimiter - 1));
				HashShaderDependency(hash, shaderRoot,
					source[delimiter] == '<' ? shaderRoot / include : key.parent_path() / include,
					visited);
				cursor = end + 1;
			}
		}

		bool HasCompleteMarker(const std::filesystem::path& directory) noexcept
		{
			std::ifstream marker(directory / "complete.txt", std::ios::binary);
			std::string markerText;
			std::getline(marker, markerText);
			return markerText == CompleteMarker;
		}

		bool Matches(
			const TextureAssetData& texture,
			uint32_t size,
			uint16_t arraySize,
			uint16_t mipLevels,
			RHIFormat format) noexcept
		{
			return texture.IsValid() &&
				texture.m_Extent.m_Width == size &&
				texture.m_Extent.m_Height == size &&
				texture.m_ArraySize == arraySize &&
				texture.m_MipLevels == mipLevels &&
				texture.m_ViewFormat == format;
		}
	}

	IBLBakeCache::IBLBakeCache(std::filesystem::path rootDirectory) noexcept :
		m_RootDirectory(std::move(rootDirectory))
	{
		utils::CreateDirectoryIfNotExist(m_RootDirectory);
	}

	uint64_t IBLBakeCache::ComputeKey(
		const std::filesystem::path& environmentPath,
		const IBLBakeConfig& config,
		std::stop_token stopToken) const noexcept
	{
		uint64_t hash = FNV1a64::OffsetBasis;
		FNV1a64::MixValue(hash, CacheFormatVersion);
		FNV1a64::MixValue(hash, BakeAlgorithmVersion);
		FNV1a64::MixValue(hash, config.m_EnvironmentCubemapSize);
		FNV1a64::MixValue(hash, config.m_EnvironmentCubemapFormat);
		FNV1a64::MixValue(hash, config.m_IrradianceCubemapSize);
		FNV1a64::MixValue(hash, config.m_IrradianceCubemapFormat);
		FNV1a64::MixValue(hash, config.m_IrradianceSampleCount);
		FNV1a64::MixValue(hash, config.m_PrefilteredSpecularCubemapSize);
		FNV1a64::MixValue(hash, config.m_PrefilteredSpecularMipLevels);
		FNV1a64::MixValue(hash, config.m_PrefilteredSpecularCubemapFormat);
		FNV1a64::MixValue(hash, config.m_PrefilteredSpecularSampleCount);
		FNV1a64::MixValue(hash, config.m_PrefilteredSpecularMaxSampleLuminance);
		FNV1a64::MixValue(hash, config.m_BrdfLutSize);
		FNV1a64::MixValue(hash, config.m_BrdfLutFormat);

		const std::filesystem::path shaderRoot = GetShaderSourceRoot();
		std::set<std::filesystem::path> visited;
		for (const auto shaderPath : BakeShaderPaths)
		{
			if (stopToken.stop_requested())
			{
				return hash;
			}
			HashShaderDependency(hash, shaderRoot, shaderRoot / shaderPath, visited);
		}

		std::ifstream stream(environmentPath, std::ios::binary);
		if (!stream)
		{
			FNV1a64::MixValue(hash, std::string_view("procedural-environment-cubemap-v1"));
			return hash;
		}

		std::array<char, 64 * 1024> buffer{};
		while (stream)
		{
			if (stopToken.stop_requested())
			{
				return hash;
			}
			stream.read(buffer.data(), buffer.size());
			const std::streamsize count = stream.gcount();
			if (count > 0)
			{
				FNV1a64::MixBytes(hash, buffer.data(), static_cast<size_t>(count));
			}
		}
		return hash;
	}

	bool IBLBakeCache::TryLoad(
		uint64_t key,
		const IBLBakeConfig& config,
		IBLBakeCachePayload& outPayload,
		std::stop_token stopToken) const noexcept
	{
		const auto directory = GetEntryDirectory(key);
		if (!HasCompleteMarker(directory))
		{
			return false;
		}

		IBLBakeCachePayload payload{};
		payload.m_Environment = TextureLoader::LoadTextureData(directory / "environment.dds", TextureColorSpace::Linear);
		if (stopToken.stop_requested())
		{
			return false;
		}
		payload.m_Irradiance = TextureLoader::LoadTextureData(directory / "irradiance.dds", TextureColorSpace::Linear);
		if (stopToken.stop_requested())
		{
			return false;
		}
		payload.m_PrefilteredSpecular = TextureLoader::LoadTextureData(directory / "prefiltered_specular.dds", TextureColorSpace::Linear);
		if (stopToken.stop_requested())
		{
			return false;
		}
		payload.m_BrdfLut = TextureLoader::LoadTextureData(directory / "brdf_lut.dds", TextureColorSpace::Linear);
		if (stopToken.stop_requested())
		{
			return false;
		}

		const uint32_t environmentSize = std::max(config.m_EnvironmentCubemapSize, 1u);
		const uint32_t specularSize = std::max(config.m_PrefilteredSpecularCubemapSize, 1u);
		const uint16_t specularMips = static_cast<uint16_t>(std::clamp(
			config.m_PrefilteredSpecularMipLevels,
			1u,
			CalculateMipLevelCount(specularSize)));
		if (!Matches(payload.m_Environment,
			environmentSize,
			static_cast<uint16_t>(CubemapFaceCount),
			static_cast<uint16_t>(CalculateMipLevelCount(environmentSize)),
			config.m_EnvironmentCubemapFormat) ||
			!Matches(payload.m_Irradiance,
				std::max(config.m_IrradianceCubemapSize, 1u),
				static_cast<uint16_t>(CubemapFaceCount),
				1,
				config.m_IrradianceCubemapFormat) ||
			!Matches(payload.m_PrefilteredSpecular,
				specularSize,
				static_cast<uint16_t>(CubemapFaceCount),
				specularMips,
				config.m_PrefilteredSpecularCubemapFormat) ||
			!Matches(payload.m_BrdfLut,
				std::max(config.m_BrdfLutSize, 1u),
				1,
				1,
				config.m_BrdfLutFormat))
		{
			GGLAB_LOG_GRAPHICS_WARN("IBL cache entry {:016x} does not match its bake configuration.", key);
			return false;
		}

		outPayload = std::move(payload);
		return true;
	}

	bool IBLBakeCache::Store(uint64_t key, const IBLBakeCachePayload& payload) const noexcept
	{
		const auto directory = GetEntryDirectory(key);
		if (HasCompleteMarker(directory))
		{
			return true;
		}

		static std::atomic_uint64_t NextTemporaryId = 0;
		const std::filesystem::path temporaryDirectory = directory.string() + ".tmp." +
			std::to_string(++NextTemporaryId);
		std::error_code errorCode;
		std::filesystem::remove_all(temporaryDirectory, errorCode);
		if (!utils::CreateDirectoryIfNotExist(temporaryDirectory))
		{
			return false;
		}

		const bool saved =
			TextureLoader::SaveTextureDataToDDS(payload.m_Environment, temporaryDirectory / "environment.dds") &&
			TextureLoader::SaveTextureDataToDDS(payload.m_Irradiance, temporaryDirectory / "irradiance.dds") &&
			TextureLoader::SaveTextureDataToDDS(payload.m_PrefilteredSpecular, temporaryDirectory / "prefiltered_specular.dds") &&
			TextureLoader::SaveTextureDataToDDS(payload.m_BrdfLut, temporaryDirectory / "brdf_lut.dds");
		if (!saved)
		{
			std::filesystem::remove_all(temporaryDirectory, errorCode);
			return false;
		}

		std::ofstream marker(temporaryDirectory / "complete.txt", std::ios::binary | std::ios::trunc);
		marker << CompleteMarker;
		marker.close();
		if (!marker)
		{
			std::filesystem::remove_all(temporaryDirectory, errorCode);
			return false;
		}

		// A completed content-addressed entry is immutable. Only replace an
		// incomplete entry, then publish the fully written directory by rename.
		if (std::filesystem::exists(directory, errorCode) && !HasCompleteMarker(directory))
		{
			std::filesystem::remove_all(directory, errorCode);
		}
		std::filesystem::rename(temporaryDirectory, directory, errorCode);
		if (errorCode && HasCompleteMarker(directory))
		{
			std::filesystem::remove_all(temporaryDirectory, errorCode);
			return true;
		}
		if (errorCode)
		{
			GGLAB_LOG_GRAPHICS_WARN("Failed to publish IBL cache entry {:016x}: {}.", key, errorCode.message());
			std::filesystem::remove_all(temporaryDirectory, errorCode);
			return false;
		}
		return true;
	}

	std::filesystem::path IBLBakeCache::GetEntryDirectory(uint64_t key) const
	{
		std::ostringstream stream;
		stream << std::hex << std::setfill('0') << std::setw(16) << key;
		return m_RootDirectory / stream.str();
	}
}
