#include "Compiler/ShaderCompiler.h"
#include "Artifact/ShaderArtifactManifestIO.h"
#include "Contracts/ShaderArtifactManifest.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabFoundation/Platform/Win/ComTypes.h"
#include "GGLabFoundation/Platform/Win/HResult.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "Targets/Vulkan13ShaderTarget.h"
#include "Targets/VulkanShaderCompileABI.h"

#include <dxcapi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gglab
{
	struct ShaderCompiler::Impl
	{
		ComPtr<IDxcUtils> m_Utils;
		ComPtr<IDxcCompiler3> m_Compiler;
	};

	namespace
	{
		inline constexpr LogTag ShaderCompilerLogTag{ "SHADER_COMPILER" };

		[[nodiscard]] constexpr std::uint64_t ReadBigEndianU64(
			const Sha256Digest& digest, std::size_t offset) noexcept
		{
			std::uint64_t value = 0;
			for (std::size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
			{
				value = (value << 8u) |
					std::to_integer<std::uint64_t>(digest.m_Value[offset + byteIndex]);
			}
			return value;
		}

		[[nodiscard]] constexpr ShaderHash128 TruncateSha256(
			const Sha256Digest& digest) noexcept
		{
			return {
				.m_LowBits = ReadBigEndianU64(digest, sizeof(std::uint64_t)),
				.m_HighBits = ReadBigEndianU64(digest, 0),
			};
		}

		[[nodiscard]] std::wstring ToHex(ShaderHash128 hash)
		{
			return std::format(L"{:016x}{:016x}", hash.m_HighBits, hash.m_LowBits);
		}

		[[nodiscard]] bool IsCompilerOwnedExtraArgument(std::wstring_view argument) noexcept
		{
			if (argument.empty())
			{
				return false;
			}
			if (argument.front() == L'@')
			{
				return true;
			}

			std::wstring lower(argument);
			std::ranges::transform(lower, lower.begin(),
				[](wchar_t value) noexcept { return static_cast<wchar_t>(::towlower(value)); });
			const auto isOption = [&lower](std::wstring_view option) noexcept
				{
					return lower == option ||
						(lower.size() > option.size() && lower.starts_with(option) &&
							lower[option.size()] == L'=');
				};

			if (lower.starts_with(L"-fvk-") || lower.starts_with(L"/fvk-") ||
				lower.starts_with(L"-fspv-") || lower.starts_with(L"/fspv-"))
			{
				return true;
			}

			constexpr std::array CompilerOwnedOptions{
				L"-e", L"/e", L"-t", L"/t", L"-hv", L"/hv", L"-spirv", L"/spirv",
				L"-zi", L"/zi", L"-qembed_debug", L"/qembed_debug", L"-qstrip_debug",
				L"/qstrip_debug", L"-qstrip_reflect", L"/qstrip_reflect", L"-fcgl",
				L"/fcgl", L"-ast-dump", L"/ast-dump", L"-dumpbin", L"/dumpbin",
			};
			if (std::ranges::any_of(CompilerOwnedOptions, isOption))
			{
				return true;
			}

			constexpr std::array OptimizationOptions{
				L"-o0", L"/o0",
				L"-o1", L"/o1",
				L"-o2", L"/o2",
				L"-o3", L"/o3",
				L"-od", L"/od",
			};
			return std::ranges::find(OptimizationOptions, lower) != OptimizationOptions.end();
		}

		class ShaderIncludeHandler final
			: public Microsoft::WRL::RuntimeClass<
			Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDxcIncludeHandler>
		{
		public:
			HRESULT RuntimeClassInitialize(ComPtr<IDxcUtils> utils,
				const std::vector<std::filesystem::path>& includeDirs) noexcept
			{
				m_Utils = utils;
				m_IncludeDirs = includeDirs;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE LoadSource(_In_z_ LPCWSTR pFilename,
				_COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource) override
			{
				const auto path = utils::Canonical(pFilename);
				ComPtr<IDxcBlobEncoding> blob;

				if (SUCCEEDED(m_Utils->LoadFile(path.c_str(), nullptr, &blob)))
				{
					m_Includes.push_back(path);
					*ppIncludeSource = blob.Detach();
					return S_OK;
				}

				for (auto& dir : m_IncludeDirs)
				{
					const auto pathInDir = utils::Canonical(dir / pFilename);
					if (SUCCEEDED(m_Utils->LoadFile(pathInDir.c_str(), nullptr, &blob)))
					{
						m_Includes.push_back(pathInDir);
						*ppIncludeSource = blob.Detach();
						return S_OK;
					}
				}

				*ppIncludeSource = nullptr;
				return E_FAIL;
			}

			const std::vector<std::filesystem::path>& Includes() const noexcept
			{
				return m_Includes;
			}

		private:
			ComPtr<IDxcUtils> m_Utils;
			std::vector<std::filesystem::path> m_IncludeDirs;
			std::vector<std::filesystem::path> m_Includes;
		};

		ShaderBinary CopyShaderBinary(IDxcBlob* blob) noexcept
		{
			if (blob == nullptr || blob->GetBufferSize() == 0)
			{
				return {};
			}

			ShaderBinary binary(blob->GetBufferSize());
			std::memcpy(binary.Data(), blob->GetBufferPointer(), blob->GetBufferSize());
			return binary;
		}

		[[nodiscard]] ShaderCompileValidationResult ValidateShaderDesc(
			const ShaderDesc& desc, const ShaderCompilerIdentity& compilerIdentity) noexcept
		{
			const auto reject = [](ShaderCompileValidationError error, std::wstring message) noexcept
				{
					return ShaderCompileValidationResult{
						.m_Error = error,
						.m_Message = std::move(message),
					};
				};

			if (desc.m_Target.m_BinaryFormat != ShaderBinaryFormat::Dxil &&
				desc.m_Target.m_BinaryFormat != ShaderBinaryFormat::SpirV)
			{
				return reject(ShaderCompileValidationError::UnsupportedBinaryFormat,
					L"Shader binary format is unsupported.");
			}
			if (desc.m_SourcePath.empty())
			{
				return reject(
					ShaderCompileValidationError::EmptySourcePath, L"Shader source path is empty.");
			}
			if (utils::Canonical(desc.m_SourcePath) != desc.m_SourcePath)
			{
				return reject(ShaderCompileValidationError::SourcePathNotCanonical,
					L"Shader source path is not canonical; normalize the descriptor before compiling.");
			}
			if (desc.m_Entry.empty())
			{
				return reject(ShaderCompileValidationError::EmptyEntryPoint,
					L"Shader entry point is empty; normalize the descriptor before compiling.");
			}
			if (compilerIdentity.m_CanonicalIdentity.empty() ||
				compilerIdentity.m_CanonicalIdentity == L"unknown")
			{
				return reject(ShaderCompileValidationError::CompilerIdentityMismatch,
					L"Active shader compiler identity is unavailable.");
			}

			if (desc.m_Target.m_BinaryFormat == ShaderBinaryFormat::SpirV)
			{
				const ShaderCompileTarget expected = MakeVulkan13CompileTarget(desc.m_Stage);
				if (desc.m_Target.m_SpirVTargetEnvironment != expected.m_SpirVTargetEnvironment)
				{
					return reject(ShaderCompileValidationError::UnsupportedSpirVTargetEnvironment,
						L"SPIR-V target environment does not match the active Vulkan shader contract.");
				}
				if (desc.m_Target.m_BindingABIRevision != expected.m_BindingABIRevision)
				{
					return reject(ShaderCompileValidationError::UnsupportedBindingABIRevision,
						L"Shader binding ABI revision does not match the active Vulkan contract.");
				}
				if (desc.m_Target.m_CoordinateOptions != expected.m_CoordinateOptions)
				{
					return reject(ShaderCompileValidationError::InvalidCoordinateOptions,
						L"Shader coordinate options do not match the active stage and backend.");
				}
			}
			else if (desc.m_Target.m_SpirVTargetEnvironment != ShaderSpirVTargetEnvironment::None ||
				desc.m_Target.m_BindingABIRevision != 0 ||
				desc.m_Target.m_CoordinateOptions != ShaderCoordinateOptions::None)
			{
				return reject(ShaderCompileValidationError::UnexpectedSpirVTargetState,
					L"DXIL shader target contains SPIR-V-only state.");
			}

			for (const std::wstring& argument : desc.m_ExtraArgs)
			{
				if (IsCompilerOwnedExtraArgument(argument))
				{
					return reject(ShaderCompileValidationError::ReservedExtraArgument,
						std::format(L"Shader extra argument '{}' is owned by the normalized target.",
							argument));
				}
			}
			return {};
		}
	}

#if defined(BUILD_DEBUG)
#define GGLAB_LOG_SHADER_COMPILER(level, ...) \
	::gglab::Log(ShaderCompilerLogTag, level, __VA_ARGS__)
#else
#define GGLAB_LOG_SHADER_COMPILER(level, ...)
#endif

	bool IsShaderBinaryFormat(
		const ShaderBinary& binary, ShaderBinaryFormat format) noexcept
	{
		if (!binary.IsValid())
		{
			return false;
		}

		const auto* data = static_cast<const uint8_t*>(binary.Data());
		const size_t size = binary.SizeInBytes();
		switch (format)
		{
		case ShaderBinaryFormat::Dxil:
			return size >= 20 && std::memcmp(data, "DXBC", 4) == 0;
		case ShaderBinaryFormat::SpirV:
		{
			if (size < 5 * sizeof(uint32_t) || size % sizeof(uint32_t) != 0)
			{
				return false;
			}
			uint32_t magic = 0;
			std::memcpy(&magic, data, sizeof(magic));
			return magic == 0x07230203u;
		}
		case ShaderBinaryFormat::Unknown:
			break;
		}
		return false;
	}

	namespace
	{
		[[nodiscard]] bool GetContainerHash(
			const void* data, size_t size, ShaderHash128& outHash) noexcept
		{
			constexpr size_t MinDxilSize = 20;
			if (data == nullptr || size < MinDxilSize)
			{
				return false;
			}

			// magic number for DirectX Container
			static const unsigned char DXBCMagicNumber[] = { 'D', 'X', 'B', 'C' };
			if (std::memcmp(data, DXBCMagicNumber, 4) != 0)
			{
				return false;
			}
			const unsigned char* ptr = static_cast<const unsigned char*>(data);
			std::memcpy(&outHash.m_LowBits, ptr + 4, sizeof(uint64_t));
			std::memcpy(&outHash.m_HighBits, ptr + 12, sizeof(uint64_t));

			return true;
		}
	}

	ShaderHash128 ComputeShaderBinaryHash(
		const ShaderBinary& binary, ShaderBinaryFormat format) noexcept
	{
		ShaderHash128 hash{};
		if (!binary.IsValid())
		{
			GGLAB_LOG_SHADER_COMPILER(LogLevel::Warning,
				"ComputeShaderBinaryHash: binary is empty.");
			return hash;
		}

		const auto* ptr = static_cast<const uint8_t*>(binary.Data());
		const auto size = binary.SizeInBytes();

		if (format == ShaderBinaryFormat::Dxil && GetContainerHash(ptr, size, hash))
		{
			return hash;
		}

		if (format == ShaderBinaryFormat::Dxil)
		{
			GGLAB_LOG_SHADER_COMPILER(LogLevel::Warning,
				"ComputeShaderBinaryHash: Failed to get DXIL container hash, fallback to SHA-256.");
		}
		return TruncateSha256(ComputeSha256(
			std::span(reinterpret_cast<const std::byte*>(ptr), size)));
	}

	ShaderCompiler::ShaderCompiler(
		std::filesystem::path sourceRoot, std::filesystem::path cacheRoot) noexcept
	{
		ComPtr<IDxcUtils> utils;
		ComPtr<IDxcCompiler3> compiler;
		const HRESULT utilsHr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
		const HRESULT compilerHr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
		if (SUCCEEDED(utilsHr) && SUCCEEDED(compilerHr))
		{
			m_Impl = std::make_unique<Impl>();
			m_Impl->m_Utils = utils;
			m_Impl->m_Compiler = compiler;
			m_CompilerIdentity.m_CanonicalIdentity = QueryDxcVersion();
		}
		else
		{
			// DXC unavailable is a recoverable CompilerUnavailable outcome;
			// construction never aborts the host process.
			m_CompilerIdentity.m_CanonicalIdentity = L"unknown";
		}

		SetSourceRootDirectory(std::move(sourceRoot));
		SetCacheRootDirectory(std::move(cacheRoot));
	}

	std::unique_ptr<ShaderCompiler> ShaderCompiler::MakeUnavailable(
		std::filesystem::path sourceRoot, std::filesystem::path cacheRoot) noexcept
	{
		std::unique_ptr<ShaderCompiler> compiler(
			new ShaderCompiler(std::move(sourceRoot), std::move(cacheRoot)));
		compiler->m_Impl.reset();
		compiler->m_CompilerIdentity.m_CanonicalIdentity = L"unknown";
		return compiler;
	}

	ShaderCompiler::~ShaderCompiler() = default;

	bool ShaderCompiler::IsCompilerAvailable() const noexcept
	{
		return m_Impl != nullptr &&
			!m_CompilerIdentity.m_CanonicalIdentity.empty() &&
			m_CompilerIdentity.m_CanonicalIdentity != L"unknown";
	}

	ShaderCompilerDiagnostics ShaderCompiler::MakeDiagnostics(ShaderCompileStatus status,
		std::wstring message, ShaderCompileValidationError validationError) const noexcept
	{
		ShaderCompilerDiagnostics diagnostics{};
		diagnostics.m_Status = status;
		diagnostics.m_ValidationError = validationError;
		diagnostics.m_Message = std::move(message);
		return diagnostics;
	}

	void ShaderCompiler::SetSourceRootDirectory(std::filesystem::path root) noexcept
	{
		m_SourceRootDir = utils::Canonical(root);
	}

	void ShaderCompiler::SetCacheRootDirectory(std::filesystem::path root) noexcept
	{
		m_CacheRootDir = utils::Canonical(root);
		// Cache root creation failure is recoverable: it surfaces later as an
		// ArtifactIOFailure result instead of aborting the host process.
		(void)utils::CreateDirectoryIfNotExist(m_CacheRootDir);
	}

	ShaderResolvedRecipe ShaderCompiler::Resolve(const ShaderDesc& request) noexcept
	{
		ShaderResolvedRecipe recipe{};
		if (!IsCompilerAvailable())
		{
			recipe.m_Status = ShaderCompileStatus::CompilerUnavailable;
			recipe.m_Diagnostics = MakeDiagnostics(ShaderCompileStatus::CompilerUnavailable,
				L"DXC compiler initialization failed or is unavailable.");
			return recipe;
		}
		return ResolveRecipe(request);
	}

	ShaderResolvedRecipe ShaderCompiler::ResolveRecipe(const ShaderDesc& request) noexcept
	{
		ShaderResolvedRecipe recipe{};

		ShaderDesc desc = request;
		if (desc.m_Entry.empty())
		{
			desc.m_Entry = DefaultEntry(desc.m_Stage);
		}

		desc.m_IncludeDirs.insert(desc.m_IncludeDirs.end(),
			m_DefaultShaderConfig.m_IncludeDirs.begin(), m_DefaultShaderConfig.m_IncludeDirs.end());
		desc.m_Defines.insert(desc.m_Defines.end(), m_DefaultShaderConfig.m_Defines.begin(),
			m_DefaultShaderConfig.m_Defines.end());
		desc.m_ExtraArgs.insert(desc.m_ExtraArgs.end(), m_DefaultShaderConfig.m_ExtraArgs.begin(),
			m_DefaultShaderConfig.m_ExtraArgs.end());
		if (desc.m_Target.m_HlslVersion.empty())
		{
			desc.m_Target.m_HlslVersion = m_DefaultShaderConfig.m_Target.m_HlslVersion;
		}
		if (desc.m_Target.m_OptimizationLevel.empty())
		{
			desc.m_Target.m_OptimizationLevel =
				m_DefaultShaderConfig.m_Target.m_OptimizationLevel;
		}
		if (desc.m_Target.m_Flags == ShaderCompileFlags::None)
		{
			desc.m_Target.m_Flags = m_DefaultShaderConfig.m_Target.m_Flags;
		}
		if (desc.m_Target.m_BinaryFormat == ShaderBinaryFormat::SpirV)
		{
			const ShaderCompileTarget vulkanTarget = MakeVulkan13CompileTarget(desc.m_Stage);
			if (desc.m_Target.m_SpirVTargetEnvironment == ShaderSpirVTargetEnvironment::None)
			{
				desc.m_Target.m_SpirVTargetEnvironment =
					vulkanTarget.m_SpirVTargetEnvironment;
			}
			if (desc.m_Target.m_BindingABIRevision == 0)
			{
				desc.m_Target.m_BindingABIRevision = vulkanTarget.m_BindingABIRevision;
			}
			desc.m_Target.m_CoordinateOptions = vulkanTarget.m_CoordinateOptions;
		}
		else
		{
			desc.m_Target.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::None;
			desc.m_Target.m_BindingABIRevision = 0;
			desc.m_Target.m_CoordinateOptions = ShaderCoordinateOptions::None;
		}

		// Source and include paths supplied by shader users are relative to the configured source root.
		if (desc.m_SourcePath.is_relative())
		{
			desc.m_SourcePath = m_SourceRootDir / desc.m_SourcePath;
		}
		desc.m_SourcePath = utils::Canonical(desc.m_SourcePath);
		for (auto& include : desc.m_IncludeDirs)
		{
			if (include.is_relative())
			{
				include = m_SourceRootDir / include;
			}
			include = utils::Canonical(include);
		}
		{
			std::unordered_set<std::wstring> seen;
			std::vector<std::filesystem::path> uniqueIncludeDirs;
			uniqueIncludeDirs.reserve(desc.m_IncludeDirs.size());
			for (auto& dir : desc.m_IncludeDirs)
			{
				auto dirStr = dir.wstring();
				if (seen.insert(dirStr).second)
				{
					uniqueIncludeDirs.emplace_back(std::move(dirStr));
				}
			}
			desc.m_IncludeDirs.swap(uniqueIncludeDirs);
		}

		// defines: sort, exclude duplicate
		std::sort(desc.m_Defines.begin(), desc.m_Defines.end());
		desc.m_Defines.erase(
			std::unique(desc.m_Defines.begin(), desc.m_Defines.end()), desc.m_Defines.end());

		// extra arguments, exclude duplicate
		{
			std::unordered_set<std::wstring> seen;
			std::vector<std::wstring> uniqueExtraArgs;
			uniqueExtraArgs.reserve(desc.m_ExtraArgs.size());
			for (auto& arg : desc.m_ExtraArgs)
			{
				if (seen.insert(arg).second)
				{
					uniqueExtraArgs.push_back(std::move(arg));
				}
			}
			desc.m_ExtraArgs.swap(uniqueExtraArgs);
		}

		const ShaderCompileValidationResult validation =
			ValidateShaderDesc(desc, m_CompilerIdentity);
		if (!validation.IsValid())
		{
			recipe.m_Status = ShaderCompileStatus::InvalidRequest;
			recipe.m_Diagnostics = MakeDiagnostics(ShaderCompileStatus::InvalidRequest,
				validation.m_Message, validation.m_Error);
			recipe.m_Diagnostics.m_SourceIdentity = desc.m_SourcePath.wstring();
			return recipe;
		}

		recipe.m_Request = std::move(desc);
		recipe.m_CompilerIdentity = m_CompilerIdentity;
		recipe.m_CompileArguments = BuildCompileArguments(recipe.m_Request);
		recipe.m_RecipeId = ComputeRecipeId(recipe.m_Request, recipe.m_CompileArguments);
		recipe.m_BuildKey = ComputeBuildKey(recipe.m_RecipeId, recipe.m_CompilerIdentity);
		return recipe;
	}

	ShaderCompileResult ShaderCompiler::CompileOrLoad(
		const ShaderResolvedRecipe& recipe) noexcept
	{
		if (!IsCompilerAvailable())
		{
			ShaderCompileResult result{};
			result.m_Status = ShaderCompileStatus::CompilerUnavailable;
			result.m_Diagnostics = MakeDiagnostics(ShaderCompileStatus::CompilerUnavailable,
				L"DXC compiler initialization failed or is unavailable.");
			return result;
		}
		return CompileOrLoadInternal(recipe);
	}

	ShaderCompileResult ShaderCompiler::Compile(const ShaderDesc& request) noexcept
	{
		const ShaderResolvedRecipe recipe = Resolve(request);
		if (!recipe.IsSuccess())
		{
			ShaderCompileResult result{};
			result.m_Status = recipe.m_Status;
			result.m_Diagnostics = recipe.m_Diagnostics;
			return result;
		}
		return CompileOrLoadInternal(recipe);
	}

	ShaderCompileResult ShaderCompiler::CompileOrLoadInternal(
		const ShaderResolvedRecipe& recipe) noexcept
	{
		ShaderCompileResult result{};
		if (!recipe.IsSuccess())
		{
			result.m_Status = recipe.m_Status;
			result.m_Diagnostics = recipe.m_Diagnostics;
			return result;
		}

		result.m_RecipeId = recipe.m_RecipeId;

		const auto keyHex = ToHex(recipe.m_BuildKey.m_Digest);
		const auto binaryPath = MakeCacheBinaryPath(
			keyHex, recipe.m_Request.m_Stage, recipe.m_Request.m_Target.m_BinaryFormat);
		auto manifestPath = binaryPath;
		manifestPath.replace_extension(L"meta.txt");

		std::error_code errorCode;
		if (std::filesystem::exists(binaryPath, errorCode) &&
			std::filesystem::exists(manifestPath, errorCode))
		{
			const std::optional<ShaderArtifact> cached =
				LoadShaderArtifact(manifestPath, binaryPath);
			if (cached.has_value() && ValidateManifestAgainstRecipe(cached->m_Manifest, recipe))
			{
				result.m_Status = ShaderCompileStatus::Success;
				result.m_Artifact = *cached;
				result.m_FromCache = true;
				return result;
			}
			GGLAB_LOG_SHADER_COMPILER(LogLevel::Info,
				"Shader cache entry rejected as invalid derived data: {}",
				utils::ToString(manifestPath.wstring()));
		}

		std::vector<std::filesystem::path> dependencies;
		ShaderCompilerDiagnostics compileDiagnostics{};
		ShaderBinary binary = CompileShaderBinary(recipe, dependencies, compileDiagnostics);
		if (!binary.IsValid() ||
			!IsShaderBinaryFormat(binary, recipe.m_Request.m_Target.m_BinaryFormat))
		{
			result.m_Status = compileDiagnostics.m_Status == ShaderCompileStatus::Success
				? ShaderCompileStatus::CompileFailed
				: compileDiagnostics.m_Status;
			result.m_Diagnostics = compileDiagnostics;
			if (result.m_Diagnostics.m_Message.empty())
			{
				result.m_Diagnostics.m_Message =
					L"DXC produced no binary or the binary format is invalid.";
			}
			result.m_Diagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return result;
		}

		ShaderArtifact artifact = BuildArtifact(recipe, binary, dependencies);
		if (!PublishShaderArtifact(binaryPath, manifestPath, artifact))
		{
			result.m_Status = ShaderCompileStatus::ArtifactIOFailure;
			result.m_Diagnostics = MakeDiagnostics(ShaderCompileStatus::ArtifactIOFailure,
				L"Shader artifact publication failed.");
			result.m_Diagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return result;
		}

		const std::optional<ShaderArtifact> published =
			LoadShaderArtifact(manifestPath, binaryPath);
		if (!published.has_value() ||
			!ValidateManifestAgainstRecipe(published->m_Manifest, recipe))
		{
			result.m_Status = ShaderCompileStatus::ArtifactIOFailure;
			result.m_Diagnostics = MakeDiagnostics(ShaderCompileStatus::ArtifactIOFailure,
				L"Published shader artifact could not be validated.");
			result.m_Diagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return result;
		}

		result.m_Status = ShaderCompileStatus::Success;
		result.m_Artifact = *published;
		result.m_FromCache = false;
		return result;
	}

	std::filesystem::path ShaderCompiler::GetCacheBinaryPath(
		const ShaderResolvedRecipe& recipe) const noexcept
	{
		return MakeCacheBinaryPath(ToHex(recipe.m_BuildKey.m_Digest),
			recipe.m_Request.m_Stage, recipe.m_Request.m_Target.m_BinaryFormat);
	}

	std::filesystem::path ShaderCompiler::MakeCacheBinaryPath(
		const std::wstring& keyHex, ShaderStage stage, ShaderBinaryFormat format) const noexcept
	{
		std::wstring stageExtension;
		switch (stage)
		{
		case ShaderStage::Vertex:
			stageExtension = L".vs";
			break;
		case ShaderStage::Pixel:
			stageExtension = L".ps";
			break;
		case ShaderStage::Hull:
			stageExtension = L".hs";
			break;
		case ShaderStage::Domain:
			stageExtension = L".ds";
			break;
		case ShaderStage::Geometry:
			stageExtension = L".gs";
			break;
		case ShaderStage::Mesh:
			stageExtension = L".ms";
			break;
		case ShaderStage::Compute:
			stageExtension = L".cs";
			break;
		default:
			break;
		}
		const std::wstring formatDirectory =
			format == ShaderBinaryFormat::SpirV ? L"spirv" : L"dxil";
		const std::wstring binaryExtension =
			format == ShaderBinaryFormat::SpirV ? L".spv" : L".dxil";
		const std::wstring extension = stageExtension + binaryExtension;

		auto path = m_CacheRootDir / formatDirectory / keyHex.substr(0, 2) /
			keyHex.substr(2, 2) / (keyHex + extension);
		(void)utils::CreateParentDirectoryIfNotExist(path);
		return path;
	}

	ShaderArtifact ShaderCompiler::BuildArtifact(const ShaderResolvedRecipe& recipe,
		const ShaderBinary& binary,
		const std::vector<std::filesystem::path>& dependencies) const noexcept
	{
		ShaderArtifact artifact{};
		ShaderArtifactManifest& manifest = artifact.m_Manifest;
		manifest.m_SchemaVersion = ShaderCacheMetadataSchema;
		manifest.m_RecipeHashSchema = ShaderRecipeHashSchema;
		manifest.m_RecipeId = recipe.m_RecipeId;
		manifest.m_BuildKey = recipe.m_BuildKey;
		manifest.m_CompilerIdentity = recipe.m_CompilerIdentity;
		manifest.m_BinaryFormat = recipe.m_Request.m_Target.m_BinaryFormat;
		manifest.m_SpirVTargetEnvironment = recipe.m_Request.m_Target.m_SpirVTargetEnvironment;
		manifest.m_BindingABIRevision = recipe.m_Request.m_Target.m_BindingABIRevision;
		manifest.m_CoordinateOptions = recipe.m_Request.m_Target.m_CoordinateOptions;
		manifest.m_Stage = recipe.m_Request.m_Stage;
		manifest.m_SourcePath = recipe.m_Request.m_SourcePath;
		manifest.m_EntryPoint = recipe.m_Request.m_Entry;
		manifest.m_TargetString = ToTarget(
			recipe.m_Request.m_Stage, recipe.m_Request.m_Target.m_Model);
		for (const ShaderDefine& define : recipe.m_Request.m_Defines)
		{
			manifest.m_Defines.push_back(define.m_Value.empty()
				? define.m_Name
				: define.m_Name + L"=" + define.m_Value);
		}
		manifest.m_IncludeDirs = recipe.m_Request.m_IncludeDirs;
		manifest.m_ExtraArgs = recipe.m_Request.m_ExtraArgs;
		manifest.m_Dependencies.push_back({
			.m_Path = recipe.m_Request.m_SourcePath,
			.m_LastWriteTimeTicks = utils::LastWriteTimeTicks(recipe.m_Request.m_SourcePath),
		});
		for (const std::filesystem::path& dependency : dependencies)
		{
			manifest.m_Dependencies.push_back({
				.m_Path = dependency,
				.m_LastWriteTimeTicks = utils::LastWriteTimeTicks(dependency),
			});
		}
		manifest.m_BinaryContentDigest.m_Digest = ComputeSha256(std::span(
			static_cast<const std::byte*>(binary.Data()), binary.SizeInBytes()));
		artifact.m_Binary = binary;
		return artifact;
	}

	bool ShaderCompiler::ValidateManifestAgainstRecipe(
		const ShaderArtifactManifest& manifest, const ShaderResolvedRecipe& recipe) const noexcept
	{
		if (manifest.m_RecipeId != recipe.m_RecipeId ||
			manifest.m_BuildKey != recipe.m_BuildKey)
		{
			return false;
		}
		if (manifest.m_CompilerIdentity.m_CanonicalIdentity !=
			recipe.m_CompilerIdentity.m_CanonicalIdentity)
		{
			return false;
		}
		if (manifest.m_BinaryFormat != recipe.m_Request.m_Target.m_BinaryFormat ||
			manifest.m_SpirVTargetEnvironment !=
				recipe.m_Request.m_Target.m_SpirVTargetEnvironment ||
			manifest.m_BindingABIRevision != recipe.m_Request.m_Target.m_BindingABIRevision ||
			manifest.m_CoordinateOptions != recipe.m_Request.m_Target.m_CoordinateOptions)
		{
			return false;
		}
		if (manifest.m_SourcePath != utils::Canonical(recipe.m_Request.m_SourcePath) ||
			manifest.m_EntryPoint != recipe.m_Request.m_Entry)
		{
			return false;
		}

		std::error_code errorCode;
		for (const ShaderArtifactDependency& dependency : manifest.m_Dependencies)
		{
			if (!std::filesystem::exists(dependency.m_Path, errorCode) ||
				utils::LastWriteTimeTicks(dependency.m_Path) != dependency.m_LastWriteTimeTicks)
			{
				return false;
			}
		}
		return true;
	}

	ShaderBinary ShaderCompiler::CompileShaderBinary(const ShaderResolvedRecipe& recipe,
		std::vector<std::filesystem::path>& outDependencies,
		ShaderCompilerDiagnostics& outDiagnostics) const noexcept
	{
		outDiagnostics.m_Status = ShaderCompileStatus::Success;

		ComPtr<IDxcBlobEncoding> src;
		if (FAILED(m_Impl->m_Utils->LoadFile(
			utils::Canonical(recipe.m_Request.m_SourcePath).c_str(), nullptr, &src)))
		{
			outDiagnostics.m_Status = ShaderCompileStatus::SourceNotFound;
			outDiagnostics.m_Message =
				L"Shader source file could not be read: " + recipe.m_Request.m_SourcePath.wstring();
			outDiagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return {};
		}

		DxcBuffer buffer{};
		buffer.Ptr = src->GetBufferPointer();
		buffer.Size = src->GetBufferSize();
		buffer.Encoding = DXC_CP_UTF8;

		ComPtr<ShaderIncludeHandler> includeHandler;
		if (FAILED(MakeAndInitialize<ShaderIncludeHandler>(
			&includeHandler, m_Impl->m_Utils, recipe.m_Request.m_IncludeDirs)))
		{
			outDiagnostics.m_Status = ShaderCompileStatus::CompileFailed;
			outDiagnostics.m_Message = L"DXC include handler initialization failed.";
			outDiagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return {};
		}

		std::vector<const wchar_t*> args;
		args.reserve(recipe.m_CompileArguments.size());
		for (const std::wstring& argument : recipe.m_CompileArguments)
		{
			args.push_back(argument.c_str());
		}

		// Compile
		ComPtr<IDxcResult> result;
		if (FAILED(m_Impl->m_Compiler->Compile(&buffer, args.data(), (UINT32)args.size(),
			includeHandler.Get(), IID_PPV_ARGS(&result))))
		{
			outDiagnostics.m_Status = ShaderCompileStatus::CompileFailed;
			outDiagnostics.m_Message = L"DXC compile invocation failed.";
			outDiagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return {};
		}

		HRESULT status = S_OK;
		result->GetStatus(&status);
		if (FAILED(status))
		{
			ComPtr<IDxcBlobUtf8> log;
			result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&log), nullptr);
			if (log && log->GetStringLength())
			{
				GGLAB_LOG_SHADER_COMPILER(LogLevel::Error,
					"DXC error:\n{}", static_cast<const char*>(log->GetBufferPointer()));
				outDiagnostics.m_Message = utils::ToWideString(std::string_view(
					static_cast<const char*>(log->GetBufferPointer()), log->GetStringLength()));
			}
			else
			{
				outDiagnostics.m_Message = L"DXC reported a compile error without diagnostics.";
			}
			outDiagnostics.m_Status = ShaderCompileStatus::CompileFailed;
			outDiagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return {};
		}

		// Record includes
		outDependencies = includeHandler->Includes();

		ComPtr<IDxcBlob> dxilBlob;
		if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&dxilBlob), nullptr)))
		{
			outDiagnostics.m_Status = ShaderCompileStatus::CompileFailed;
			outDiagnostics.m_Message = L"DXC produced no output object.";
			outDiagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return {};
		}

		return CopyShaderBinary(dxilBlob.Get());
	}

	std::vector<std::wstring> ShaderCompiler::BuildCompileArguments(const ShaderDesc& desc) noexcept
	{
		const ShaderCompileTarget& compileTarget = desc.m_Target;
		std::vector<std::wstring> args;
		args.reserve(32 + desc.m_IncludeDirs.size() * 2 + desc.m_Defines.size() * 2 +
			desc.m_ExtraArgs.size());

		args.emplace_back(L"-E");
		args.push_back(desc.m_Entry.empty() ? DefaultEntry(desc.m_Stage) : desc.m_Entry);
		args.emplace_back(L"-T");
		args.push_back(ToTarget(desc.m_Stage, compileTarget.m_Model));
		args.emplace_back(L"-HV");
		args.push_back(compileTarget.m_HlslVersion);

		if (compileTarget.m_BinaryFormat == ShaderBinaryFormat::SpirV)
		{
			args.emplace_back(L"-spirv");
			args.emplace_back(L"-fspv-target-env=vulkan1.3");
			args.emplace_back(L"-fvk-use-dx-layout");

			const auto appendRegisterShift = [&args](std::wstring_view option,
				VulkanShaderRegisterClass registerClass) noexcept
				{
					const VulkanFixedRegisterRange range =
						GetVulkanFixedRegisterRange(registerClass);
					args.emplace_back(option);
					args.push_back(std::to_wstring(range.m_BindingShift));
					args.push_back(std::to_wstring(
						GGLabVulkanShaderCompileABI.m_FixedHlslRegisterSpace));
				};
			appendRegisterShift(L"-fvk-b-shift", VulkanShaderRegisterClass::ConstantBuffer);
			appendRegisterShift(L"-fvk-t-shift", VulkanShaderRegisterClass::ShaderResource);
			appendRegisterShift(L"-fvk-u-shift", VulkanShaderRegisterClass::UnorderedAccess);
			appendRegisterShift(L"-fvk-s-shift", VulkanShaderRegisterClass::Sampler);

			args.emplace_back(L"-fvk-bind-resource-heap");
			args.push_back(std::to_wstring(GGLabVulkanShaderCompileABI.m_ResourceHeapBinding));
			args.push_back(std::to_wstring(GGLabVulkanShaderCompileABI.m_GlobalDescriptorSet));
			args.emplace_back(L"-fvk-bind-sampler-heap");
			args.push_back(std::to_wstring(GGLabVulkanShaderCompileABI.m_SamplerHeapBinding));
			args.push_back(std::to_wstring(GGLabVulkanShaderCompileABI.m_GlobalDescriptorSet));

			if (Test(compileTarget.m_CoordinateOptions, ShaderCoordinateOptions::InvertY))
			{
				args.emplace_back(L"-fvk-invert-y");
			}
			if (Test(compileTarget.m_CoordinateOptions, ShaderCoordinateOptions::UseDxPositionW))
			{
				args.emplace_back(L"-fvk-use-dx-position-w");
			}
		}

		if (Test(compileTarget.m_Flags, ShaderCompileFlags::Debug))
		{
			args.emplace_back(DXC_ARG_DEBUG);
			if (compileTarget.m_BinaryFormat == ShaderBinaryFormat::SpirV)
			{
				args.emplace_back(L"-fspv-debug=vulkan-with-source");
			}
			else
			{
				args.emplace_back(L"-Qembed_debug");
			}
		}
		else if (compileTarget.m_BinaryFormat == ShaderBinaryFormat::Dxil)
		{
			args.emplace_back(L"-Qstrip_debug");
			args.emplace_back(L"-Qstrip_reflect");
		}

		if (Test(compileTarget.m_Flags, ShaderCompileFlags::Optimization))
		{
			args.push_back(L"-" + compileTarget.m_OptimizationLevel);
		}
		else
		{
			args.emplace_back(DXC_ARG_SKIP_OPTIMIZATIONS);
		}

		for (const auto& include : desc.m_IncludeDirs)
		{
			args.emplace_back(L"-I");
			args.push_back(utils::Canonical(include).wstring());
		}
		for (const ShaderDefine& define : desc.m_Defines)
		{
			std::wstring value = define.m_Name;
			if (!define.m_Value.empty())
			{
				value += L"=" + define.m_Value;
			}
			args.emplace_back(L"-D");
			args.push_back(std::move(value));
		}
		args.insert(args.end(), desc.m_ExtraArgs.begin(), desc.m_ExtraArgs.end());
		return args;
	}

	ShaderRecipeId ShaderCompiler::ComputeRecipeId(
		const ShaderDesc& mergedDesc, const std::vector<std::wstring>& compileArguments) noexcept
	{
		Sha256Builder builder;
		bool succeeded = builder.AddStringUtf8("gglab.shader.recipe") &&
			builder.AddU32LE(ShaderRecipeHashSchema) &&
			builder.AddStringUtf8(
				utils::ToString(utils::Canonical(mergedDesc.m_SourcePath).generic_wstring())) &&
			builder.AddU32LE(static_cast<std::uint32_t>(
				mergedDesc.m_Target.m_BinaryFormat)) &&
			builder.AddU32LE(static_cast<std::uint32_t>(
				mergedDesc.m_Target.m_SpirVTargetEnvironment)) &&
			builder.AddU32LE(mergedDesc.m_Target.m_BindingABIRevision) &&
			builder.AddU32LE(static_cast<std::uint32_t>(
				mergedDesc.m_Target.m_CoordinateOptions)) &&
			builder.AddU64LE(static_cast<std::uint64_t>(compileArguments.size()));
		for (const std::wstring& argument : compileArguments)
		{
			succeeded = succeeded && builder.AddStringUtf8(utils::ToString(argument));
		}

		GGLAB_ASSERT_MSG(succeeded, "Failed to encode the shader recipe identity input.");
		ShaderRecipeId recipeId{};
		recipeId.m_Digest = TruncateSha256(builder.Finish());
		return recipeId;
	}

	LocalShaderCacheKey ShaderCompiler::ComputeBuildKey(
		const ShaderRecipeId& recipeId, const ShaderCompilerIdentity& compilerIdentity) noexcept
	{
		Sha256Builder builder;
		bool succeeded = builder.AddStringUtf8("gglab.shader.buildkey") &&
			builder.AddU32LE(ShaderRecipeHashSchema) &&
			builder.AddU64LE(recipeId.m_Digest.m_LowBits) &&
			builder.AddU64LE(recipeId.m_Digest.m_HighBits) &&
			builder.AddStringUtf8(utils::ToString(compilerIdentity.m_CanonicalIdentity));

		GGLAB_ASSERT_MSG(succeeded, "Failed to encode the shader build key input.");
		LocalShaderCacheKey buildKey{};
		buildKey.m_Digest = TruncateSha256(builder.Finish());
		return buildKey;
	}

	std::wstring ShaderCompiler::DefaultEntry(const ShaderStage& stage) noexcept
	{
		switch (stage)
		{
		case ShaderStage::Vertex:
			return L"VSMain";
		case ShaderStage::Pixel:
			return L"PSMain";
		case ShaderStage::Hull:
			return L"HSMain";
		case ShaderStage::Domain:
			return L"DSMain";
		case ShaderStage::Geometry:
			return L"GSMain";
		case ShaderStage::Mesh:
			return L"MSMain";
		case ShaderStage::Compute:
			return L"CSMain";
		default:
			break;
		}
		return L"Main";
	}

	std::wstring ShaderCompiler::ToTarget(ShaderStage stage, ShaderModel model) noexcept
	{
		std::wstring target;
		switch (stage)
		{
		case ShaderStage::Vertex:
			target += L"vs_";
			break;
		case ShaderStage::Pixel:
			target += L"ps_";
			break;
		case ShaderStage::Hull:
			target += L"hs_";
			break;
		case ShaderStage::Domain:
			target += L"ds_";
			break;
		case ShaderStage::Geometry:
			target += L"gs_";
			break;
		case ShaderStage::Mesh:
			target += L"ms_";
			break;
		case ShaderStage::Compute:
			target += L"cs_";
			break;
		default:
			GGLAB_UNREACHABLE("Unknown ShaderStage.");
		}

		switch (model)
		{
		case ShaderModel::SM_6_6:
			target += L"6_6";
			break;
		case ShaderModel::SM_6_7:
			target += L"6_7";
			break;
		case ShaderModel::SM_6_8:
			target += L"6_8";
			break;
		default:
			GGLAB_UNREACHABLE("Unknown ShaderModel.");
		}

		return target;
	}

	std::wstring ShaderCompiler::QueryDxcVersion() const noexcept
	{
		if (m_Impl == nullptr)
		{
			return L"unknown";
		}

		ComPtr<IDxcVersionInfo> versionInfo;
		if (FAILED(m_Impl->m_Compiler.As(&versionInfo)))
		{
			return L"unknown";
		}

		UINT32 major = 0;
		UINT32 minor = 0;
		if (FAILED(versionInfo->GetVersion(&major, &minor)))
		{
			return L"unknown";
		}

		ComPtr<IDxcVersionInfo2> versionInfo2;
		if (FAILED(m_Impl->m_Compiler.As(&versionInfo2)))
		{
			return std::format(L"{}.{}", major, minor);
		}

		UINT32 commitCount = 0;
		char* commitHash = nullptr;
		if (FAILED(versionInfo2->GetCommitInfo(&commitCount, &commitHash)))
		{
			return std::format(L"{}.{}", major, minor);
		}
		const std::wstring commit =
			commitHash ? utils::ToWideString(commitHash) : std::wstring(L"unknown");
		CoTaskMemFree(commitHash);
		return std::format(L"{}.{}+{}.{}", major, minor, commitCount, commit);
	}
}

#undef GGLAB_LOG_SHADER_COMPILER
