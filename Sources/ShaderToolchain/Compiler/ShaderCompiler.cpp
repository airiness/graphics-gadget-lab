#include "Compiler/ShaderCompiler.h"
#include "Artifact/ShaderArtifactManifestIO.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabFoundation/Platform/Win/ComTypes.h"
#include "GGLabFoundation/Platform/Win/HResult.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "ShaderArtifactRuntime/ShaderArtifactManifest.h"
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
#include <fstream>
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
			// Physical read location plus the digest of the exact bytes handed
			// to DXC. The portable logical identity is derived afterwards
			// relative to the source root.
			struct RecordedInclude
			{
				std::filesystem::path m_PhysicalPath{};
				Sha256Digest m_ContentDigest{};
			};

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
					RecordDependency(path, blob.Get());
					*ppIncludeSource = blob.Detach();
					return S_OK;
				}

				for (auto& dir : m_IncludeDirs)
				{
					const auto pathInDir = utils::Canonical(dir / pFilename);
					if (SUCCEEDED(m_Utils->LoadFile(pathInDir.c_str(), nullptr, &blob)))
					{
						RecordDependency(pathInDir, blob.Get());
						*ppIncludeSource = blob.Detach();
						return S_OK;
					}
				}

				*ppIncludeSource = nullptr;
				return E_FAIL;
			}

			const std::vector<RecordedInclude>& Dependencies() const noexcept
			{
				return m_Dependencies;
			}

		private:
			// The dependency digest is computed over the exact bytes handed to
			// DXC, so cache validation can never drift from what was actually
			// compiled, regardless of when files change on disk.
			void RecordDependency(const std::filesystem::path& path, IDxcBlob* blob) noexcept
			{
				RecordedInclude dependency{};
				dependency.m_PhysicalPath = path;
				dependency.m_ContentDigest = ComputeSha256(std::span(
					static_cast<const std::byte*>(blob->GetBufferPointer()),
					blob->GetBufferSize()));
				m_Dependencies.push_back(std::move(dependency));
			}

			ComPtr<IDxcUtils> m_Utils;
			std::vector<std::filesystem::path> m_IncludeDirs;
			std::vector<RecordedInclude> m_Dependencies;
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

		// Portable source identity: the request carries a logical path
		// relative to the source root; the physical path is derived for IO and
		// diagnostics only and never participates in the recipe identity.
		if (!desc.m_SourcePath.is_relative())
		{
			recipe.m_Status = ShaderCompileStatus::InvalidRequest;
			recipe.m_Diagnostics = MakeDiagnostics(ShaderCompileStatus::InvalidRequest,
				L"Shader source path must be a logical path relative to the source root.",
				ShaderCompileValidationError::SourcePathNotCanonical);
			recipe.m_Diagnostics.m_SourceIdentity = desc.m_SourcePath.wstring();
			return recipe;
		}
		const std::filesystem::path logicalSourcePath = desc.m_SourcePath.lexically_normal();
		for (const std::filesystem::path& component : logicalSourcePath)
		{
			if (component == L"..")
			{
				recipe.m_Status = ShaderCompileStatus::InvalidRequest;
				recipe.m_Diagnostics = MakeDiagnostics(ShaderCompileStatus::InvalidRequest,
					L"Shader logical source path must stay inside the source root.",
					ShaderCompileValidationError::SourcePathNotCanonical);
				recipe.m_Diagnostics.m_SourceIdentity = desc.m_SourcePath.wstring();
				return recipe;
			}
		}
		desc.m_SourcePath = utils::Canonical(m_SourceRootDir / logicalSourcePath);

		// Logical include configuration: the identity-relevant form is the
		// caller-relative path. Absolute include dirs are relativized against
		// the source root when they live under it, so checkout locations never
		// enter the recipe identity.
		std::vector<std::filesystem::path> logicalIncludeDirs;
		logicalIncludeDirs.reserve(desc.m_IncludeDirs.size());
		for (const std::filesystem::path& include : desc.m_IncludeDirs)
		{
			if (include.is_relative())
			{
				logicalIncludeDirs.push_back(include.lexically_normal());
				continue;
			}
			const std::filesystem::path canonicalInclude = utils::Canonical(include);
			std::error_code relativeError;
			const std::filesystem::path relativeInclude = std::filesystem::relative(
				canonicalInclude, m_SourceRootDir, relativeError);
			logicalIncludeDirs.push_back(
				relativeError ? canonicalInclude : relativeInclude.lexically_normal());
		}
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
		recipe.m_LogicalSourcePath = logicalSourcePath;
		recipe.m_LogicalIncludeDirs = std::move(logicalIncludeDirs);
		recipe.m_CompilerIdentity = m_CompilerIdentity;
		recipe.m_CompileArguments = BuildCompileArguments(recipe.m_Request);
		recipe.m_RecipeId = ComputeRecipeId(
			recipe.m_LogicalSourcePath, recipe.m_LogicalIncludeDirs, recipe.m_Request);
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

		// Authority gate: the recipe must belong to this producer and remain an
		// internally consistent resolved state. A recipe from another compiler
		// instance, or one mutated after Resolve, is rejected instead of being
		// re-interpreted (which would publish new content under a stale
		// identity).
		ShaderCompilerDiagnostics authorityDiagnostics{};
		if (!ValidateRecipeAuthority(recipe, authorityDiagnostics))
		{
			result.m_Status = authorityDiagnostics.m_Status;
			result.m_Diagnostics = authorityDiagnostics;
			return result;
		}

		result.m_RecipeId = recipe.m_RecipeId;

		const std::wstring keyHex = utils::ToWideString(
			Sha256DigestToHex(recipe.m_BuildKey.m_DurableDigest));
		const auto binaryPath = MakeCacheBinaryPath(
			keyHex, recipe.m_Request.m_Stage, recipe.m_Request.m_Target.m_BinaryFormat);
		auto recordPath = binaryPath;
		recordPath += L".json";

		std::error_code errorCode;
		if (std::filesystem::exists(binaryPath, errorCode) &&
			std::filesystem::exists(recordPath, errorCode))
		{
			const std::optional<ShaderArtifactCacheRecord> cached =
				LoadShaderArtifactCacheRecord(recordPath, binaryPath);
			if (cached.has_value() && ValidateCacheRecordAgainstRecipe(*cached, recipe))
			{
				result.m_Status = ShaderCompileStatus::Success;
				result.m_Artifact.m_Manifest = cached->m_Manifest;
				result.m_Artifact.m_Binary = cached->m_Binary;
				result.m_FromCache = true;
				return result;
			}
			GGLAB_LOG_SHADER_COMPILER(LogLevel::Info,
				"Shader cache entry rejected as invalid derived data: {}",
				utils::ToString(recordPath.wstring()));
		}

		std::vector<ShaderArtifactDependency> dependencies;
		std::vector<std::filesystem::path> dependencyPhysicalPaths;
		ShaderCompilerDiagnostics compileDiagnostics{};
		ShaderBinary binary = CompileShaderBinary(
			recipe, dependencies, dependencyPhysicalPaths, compileDiagnostics);
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

		ShaderArtifactCacheRecord record =
			BuildCacheRecord(recipe, binary, dependencies, dependencyPhysicalPaths);

		// Publication handoff: the publication operation performs the publish
		// attempt, the final committed-entry observation, and the outcome
		// classification. Success hands off the observed structurally
		// validated committed record; CompileOrLoad never rereads the slot
		// and never returns an uncommitted compile product.
		const ShaderPublicationResult publication =
			PublishShaderArtifactCacheRecord(binaryPath, recordPath, record);
		if (publication.m_Outcome == ShaderPublicationOutcome::Failed)
		{
			result.m_Status = ShaderCompileStatus::ArtifactIOFailure;
			result.m_Diagnostics = MakeDiagnostics(ShaderCompileStatus::ArtifactIOFailure,
				L"Shader artifact publication failed.");
			result.m_Diagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return result;
		}
		if (publication.m_Outcome == ShaderPublicationOutcome::Published)
		{
			result.m_Status = ShaderCompileStatus::Success;
			result.m_Artifact.m_Manifest = publication.m_CommittedRecord.m_Manifest;
			result.m_Artifact.m_Binary = publication.m_CommittedRecord.m_Binary;
			result.m_FromCache = false;
			return result;
		}

		// CommittedByOther: the competing committed entry is structurally
		// valid, but Success additionally requires the input freshness
		// closure: its dependency provenance must equal the provenance this
		// operation's compile stage actually read.
		if (publication.m_CommittedRecord.m_Manifest.m_Dependencies ==
			record.m_Manifest.m_Dependencies)
		{
			result.m_Status = ShaderCompileStatus::Success;
			result.m_Artifact.m_Manifest = publication.m_CommittedRecord.m_Manifest;
			result.m_Artifact.m_Binary = publication.m_CommittedRecord.m_Binary;
			result.m_FromCache = true;
			return result;
		}

		// Provenance conflict: republish this operation's freshly compiled
		// product once. The second publication runs the same attempt +
		// observation + classification protocol.
		const ShaderPublicationResult republished =
			PublishShaderArtifactCacheRecord(binaryPath, recordPath, record);
		if (republished.m_Outcome == ShaderPublicationOutcome::Failed)
		{
			result.m_Status = ShaderCompileStatus::ArtifactIOFailure;
			result.m_Diagnostics = MakeDiagnostics(ShaderCompileStatus::ArtifactIOFailure,
				L"Shader artifact publication failed.");
			result.m_Diagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return result;
		}
		if (republished.m_Outcome == ShaderPublicationOutcome::Published)
		{
			result.m_Status = ShaderCompileStatus::Success;
			result.m_Artifact.m_Manifest = republished.m_CommittedRecord.m_Manifest;
			result.m_Artifact.m_Binary = republished.m_CommittedRecord.m_Binary;
			result.m_FromCache = false;
			return result;
		}
		if (republished.m_CommittedRecord.m_Manifest.m_Dependencies ==
			record.m_Manifest.m_Dependencies)
		{
			result.m_Status = ShaderCompileStatus::Success;
			result.m_Artifact.m_Manifest = republished.m_CommittedRecord.m_Manifest;
			result.m_Artifact.m_Binary = republished.m_CommittedRecord.m_Binary;
			result.m_FromCache = true;
			return result;
		}

		// The conflict did not resolve: a structurally valid committed
		// artifact exists, but its dependency provenance still differs from
		// what this operation compiled. Retryable failure, not an IO error.
		result.m_Status = ShaderCompileStatus::SourceChangedDuringCompile;
		result.m_Diagnostics = MakeDiagnostics(ShaderCompileStatus::SourceChangedDuringCompile,
			L"Competing committed artifact has different dependency provenance.");
		result.m_Diagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
		return result;
	}

	bool ShaderCompiler::ValidateRecipeAuthority(
		const ShaderResolvedRecipe& recipe, ShaderCompilerDiagnostics& outDiagnostics) const noexcept
	{
		if (recipe.m_CompilerIdentity.m_CanonicalIdentity !=
			m_CompilerIdentity.m_CanonicalIdentity)
		{
			outDiagnostics = MakeDiagnostics(ShaderCompileStatus::InvalidRequest,
				L"Recipe producer identity does not match the active compiler.",
				ShaderCompileValidationError::CompilerIdentityMismatch);
			outDiagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return false;
		}

		const std::vector<std::wstring> rederivedArguments =
			BuildCompileArguments(recipe.m_Request);
		const ShaderRecipeId rederivedRecipeId = ComputeRecipeId(
			recipe.m_LogicalSourcePath, recipe.m_LogicalIncludeDirs, recipe.m_Request);
		const LocalShaderCacheKey rederivedBuildKey =
			ComputeBuildKey(rederivedRecipeId, m_CompilerIdentity);
		const std::filesystem::path expectedPhysicalSource = utils::Canonical(
			m_SourceRootDir / recipe.m_LogicalSourcePath);

		if (rederivedArguments != recipe.m_CompileArguments ||
			rederivedRecipeId != recipe.m_RecipeId ||
			rederivedBuildKey != recipe.m_BuildKey ||
			expectedPhysicalSource != utils::Canonical(recipe.m_Request.m_SourcePath))
		{
			outDiagnostics = MakeDiagnostics(ShaderCompileStatus::InvalidRequest,
				L"Resolved recipe is not an internally consistent resolved state.");
			outDiagnostics.m_SourceIdentity = recipe.m_Request.m_SourcePath.wstring();
			return false;
		}
		return true;
	}

	std::filesystem::path ShaderCompiler::GetCacheBinaryPath(
		const ShaderResolvedRecipe& recipe) const noexcept
	{
		const std::wstring keyHex = utils::ToWideString(
			Sha256DigestToHex(recipe.m_BuildKey.m_DurableDigest));
		return MakeCacheBinaryPath(
			keyHex, recipe.m_Request.m_Stage, recipe.m_Request.m_Target.m_BinaryFormat);
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

	ShaderArtifactCacheRecord ShaderCompiler::BuildCacheRecord(const ShaderResolvedRecipe& recipe,
		const ShaderBinary& binary,
		const std::vector<ShaderArtifactDependency>& dependencies,
		const std::vector<std::filesystem::path>& dependencyPhysicalPaths) const noexcept
	{
		ShaderArtifactCacheRecord record{};
		ShaderArtifactManifest& manifest = record.m_Manifest;
		manifest.m_SchemaVersion = ShaderArtifactManifestSchemaVersion;
		manifest.m_RecipeHashSchema = ShaderRecipeHashSchema;
		manifest.m_RecipeId = recipe.m_RecipeId;
		manifest.m_BuildKey = recipe.m_BuildKey;
		manifest.m_CompilerIdentity = recipe.m_CompilerIdentity;
		manifest.m_TargetProfile = GetShaderTargetProfile(
			recipe.m_Request.m_Target.m_BinaryFormat,
			recipe.m_Request.m_Target.m_SpirVTargetEnvironment);
		manifest.m_BinaryFormat = recipe.m_Request.m_Target.m_BinaryFormat;
		manifest.m_SpirVTargetEnvironment = recipe.m_Request.m_Target.m_SpirVTargetEnvironment;
		manifest.m_BindingABIRevision = recipe.m_Request.m_Target.m_BindingABIRevision;
		manifest.m_CoordinateOptions = recipe.m_Request.m_Target.m_CoordinateOptions;
		manifest.m_Stage = recipe.m_Request.m_Stage;
		manifest.m_ShaderModel = recipe.m_Request.m_Target.m_Model;
		manifest.m_HlslVersion = recipe.m_Request.m_Target.m_HlslVersion;
		manifest.m_CompileFlags = recipe.m_Request.m_Target.m_Flags;
		manifest.m_OptimizationLevel = recipe.m_Request.m_Target.m_OptimizationLevel;
		manifest.m_LogicalSourcePath = recipe.m_LogicalSourcePath;
		manifest.m_EntryPoint = recipe.m_Request.m_Entry;
		manifest.m_TargetString = ToTarget(
			recipe.m_Request.m_Stage, recipe.m_Request.m_Target.m_Model);
		for (const ShaderDefine& define : recipe.m_Request.m_Defines)
		{
			manifest.m_Defines.push_back(define.m_Value.empty()
				? define.m_Name
				: define.m_Name + L"=" + define.m_Value);
		}
		manifest.m_LogicalIncludeDirs = recipe.m_LogicalIncludeDirs;
		manifest.m_ExtraArgs = recipe.m_Request.m_ExtraArgs;
		manifest.m_BinaryContentDigest.m_Digest = ComputeSha256(std::span(
			static_cast<const std::byte*>(binary.Data()), binary.SizeInBytes()));
		manifest.m_Dependencies = dependencies;

		record.m_Binary = binary;
		record.m_PhysicalSourcePath = recipe.m_Request.m_SourcePath;
		record.m_PhysicalIncludeDirs = recipe.m_Request.m_IncludeDirs;
		record.m_DependencyPhysicalPaths = dependencyPhysicalPaths;
		return record;
	}

	bool ShaderCompiler::ValidateCacheRecordAgainstRecipe(
		const ShaderArtifactCacheRecord& record, const ShaderResolvedRecipe& recipe) const noexcept
	{
		const ShaderArtifactManifest& manifest = record.m_Manifest;
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
			manifest.m_CoordinateOptions != recipe.m_Request.m_Target.m_CoordinateOptions ||
			manifest.m_TargetProfile != GetShaderTargetProfile(
				recipe.m_Request.m_Target.m_BinaryFormat,
				recipe.m_Request.m_Target.m_SpirVTargetEnvironment))
		{
			return false;
		}
		if (manifest.m_LogicalSourcePath != recipe.m_LogicalSourcePath ||
			manifest.m_EntryPoint != recipe.m_Request.m_Entry ||
			manifest.m_ShaderModel != recipe.m_Request.m_Target.m_Model ||
			manifest.m_HlslVersion != recipe.m_Request.m_Target.m_HlslVersion ||
			manifest.m_CompileFlags != recipe.m_Request.m_Target.m_Flags ||
			manifest.m_OptimizationLevel != recipe.m_Request.m_Target.m_OptimizationLevel ||
			manifest.m_LogicalIncludeDirs != recipe.m_LogicalIncludeDirs ||
			record.m_PhysicalSourcePath != utils::Canonical(recipe.m_Request.m_SourcePath))
		{
			return false;
		}

		// Dependency validation: the content digest is the sole authority, and
		// no mtime participates in any cache-hit decision. A candidate is
		// accepted only when a digest of the exact current bytes equals the
		// digest of the bytes the compiler consumed. Each portable dependency
		// pairs index-wise with its local physical resolution; a cardinality
		// mismatch is treated as invalid derived data.
		if (record.m_Manifest.m_Dependencies.size() !=
			record.m_DependencyPhysicalPaths.size())
		{
			return false;
		}
		// Main-source binding defense: the first dependency must describe the
		// recipe's own source, in both its portable and local forms, so an
		// empty or mismatched record can never skip source validation.
		if (record.m_Manifest.m_Dependencies.empty() ||
			record.m_Manifest.m_Dependencies[0].m_LogicalPath !=
				recipe.m_LogicalSourcePath ||
			utils::Canonical(record.m_DependencyPhysicalPaths[0]) !=
				utils::Canonical(record.m_PhysicalSourcePath))
		{
			return false;
		}
		std::error_code errorCode;
		for (std::size_t index = 0; index < record.m_Manifest.m_Dependencies.size(); ++index)
		{
			const ShaderArtifactDependency& dependency =
				record.m_Manifest.m_Dependencies[index];
			const std::filesystem::path& physicalPath =
				record.m_DependencyPhysicalPaths[index];
			if (!std::filesystem::exists(physicalPath, errorCode))
			{
				return false;
			}
			std::ifstream input(physicalPath, std::ios::binary);
			if (!input)
			{
				return false;
			}
			Sha256Builder builder;
			std::array<char, 16 * 1024> buffer{};
			while (input)
			{
				input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
				const std::streamsize count = input.gcount();
				if (count <= 0)
				{
					break;
				}
				if (!builder.AddBytes(std::span(
					reinterpret_cast<const std::byte*>(buffer.data()),
					static_cast<std::size_t>(count))))
				{
					return false;
				}
			}
			if (builder.Finish() != dependency.m_ContentDigest)
			{
				return false;
			}
		}
		return true;
	}

	ShaderBinary ShaderCompiler::CompileShaderBinary(const ShaderResolvedRecipe& recipe,
		std::vector<ShaderArtifactDependency>& outDependencies,
		std::vector<std::filesystem::path>& outDependencyPhysicalPaths,
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

		// The source dependency digest is computed over the exact bytes DXC is
		// about to receive and is the sole validation authority for cache
		// hits. The portable provenance carries the logical identity and the
		// digest; the physical path is local cache state kept in a parallel
		// list.
		outDependencies.push_back({
			.m_LogicalPath = recipe.m_LogicalSourcePath,
			.m_ContentDigest = ComputeSha256(std::span(
				static_cast<const std::byte*>(src->GetBufferPointer()),
				src->GetBufferSize())),
		});
		outDependencyPhysicalPaths.push_back(
			utils::Canonical(recipe.m_Request.m_SourcePath));

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

		// Record include dependencies: the digest is the sole validation
		// authority. The portable logical identity is derived relative to the
		// source root when possible; the physical path goes to the parallel
		// local list.
		for (const ShaderIncludeHandler::RecordedInclude& includeDependency :
			includeHandler->Dependencies())
		{
			ShaderArtifactDependency dependency{};
			dependency.m_ContentDigest = includeDependency.m_ContentDigest;
			std::error_code relativeError;
			const std::filesystem::path relativeInclude = std::filesystem::relative(
				includeDependency.m_PhysicalPath, m_SourceRootDir, relativeError);
			if (!relativeError)
			{
				dependency.m_LogicalPath = relativeInclude.lexically_normal();
			}
			outDependencies.push_back(std::move(dependency));
			outDependencyPhysicalPaths.push_back(includeDependency.m_PhysicalPath);
		}

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

	ShaderRecipeId ShaderCompiler::ComputeRecipeId(const std::filesystem::path& logicalSourcePath,
		const std::vector<std::filesystem::path>& logicalIncludeDirs,
		const ShaderDesc& mergedDesc) noexcept
	{
		// Logical request encoding only: no physical checkout paths, no
		// physical -I arguments. The same logical request must produce the
		// same identity from any checkout location.
		Sha256Builder builder;
		bool succeeded = builder.AddStringUtf8("gglab.shader.recipe") &&
			builder.AddU32LE(ShaderRecipeHashSchema) &&
			builder.AddStringUtf8(utils::ToString(logicalSourcePath.generic_wstring())) &&
			builder.AddStringUtf8(utils::ToString(mergedDesc.m_Entry)) &&
			builder.AddU32LE(static_cast<std::uint32_t>(mergedDesc.m_Stage)) &&
			builder.AddU32LE(static_cast<std::uint32_t>(
				mergedDesc.m_Target.m_BinaryFormat)) &&
			builder.AddU32LE(static_cast<std::uint32_t>(
				mergedDesc.m_Target.m_SpirVTargetEnvironment)) &&
			builder.AddU32LE(mergedDesc.m_Target.m_BindingABIRevision) &&
			builder.AddU32LE(static_cast<std::uint32_t>(
				mergedDesc.m_Target.m_CoordinateOptions)) &&
			builder.AddU32LE(static_cast<std::uint32_t>(
				mergedDesc.m_Target.m_Model)) &&
			builder.AddStringUtf8(utils::ToString(mergedDesc.m_Target.m_HlslVersion)) &&
			builder.AddU32LE(static_cast<std::uint32_t>(
				mergedDesc.m_Target.m_Flags)) &&
			builder.AddStringUtf8(utils::ToString(mergedDesc.m_Target.m_OptimizationLevel)) &&
			builder.AddU64LE(static_cast<std::uint64_t>(logicalIncludeDirs.size()));
		for (const std::filesystem::path& includeDir : logicalIncludeDirs)
		{
			succeeded = succeeded &&
				builder.AddStringUtf8(utils::ToString(includeDir.generic_wstring()));
		}
		succeeded = succeeded &&
			builder.AddU64LE(static_cast<std::uint64_t>(mergedDesc.m_Defines.size()));
		for (const ShaderDefine& define : mergedDesc.m_Defines)
		{
			succeeded = succeeded &&
				builder.AddStringUtf8(utils::ToString(
					define.m_Value.empty() ? define.m_Name : define.m_Name + L"=" + define.m_Value));
		}
		succeeded = succeeded &&
			builder.AddU64LE(static_cast<std::uint64_t>(mergedDesc.m_ExtraArgs.size()));
		for (const std::wstring& argument : mergedDesc.m_ExtraArgs)
		{
			succeeded = succeeded && builder.AddStringUtf8(utils::ToString(argument));
		}

		GGLAB_ASSERT_MSG(succeeded, "Failed to encode the shader recipe identity input.");
		ShaderRecipeId recipeId{};
		recipeId.m_DurableDigest = builder.Finish();
		return recipeId;
	}

	LocalShaderCacheKey ShaderCompiler::ComputeBuildKey(
		const ShaderRecipeId& recipeId, const ShaderCompilerIdentity& compilerIdentity) noexcept
	{
		Sha256Builder builder;
		bool succeeded = builder.AddStringUtf8("gglab.shader.buildkey") &&
			builder.AddU32LE(ShaderRecipeHashSchema) &&
			builder.AddBytes(std::span(recipeId.m_DurableDigest.m_Value)) &&
			builder.AddStringUtf8(utils::ToString(compilerIdentity.m_CanonicalIdentity));

		GGLAB_ASSERT_MSG(succeeded, "Failed to encode the shader build key input.");
		LocalShaderCacheKey buildKey{};
		buildKey.m_DurableDigest = builder.Finish();
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

	namespace
	{
		[[nodiscard]] std::wstring QueryDxcVersionFrom(
			const ComPtr<IDxcCompiler3>& compiler) noexcept
		{
			ComPtr<IDxcVersionInfo> versionInfo;
			if (FAILED(compiler.As(&versionInfo)))
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
			if (FAILED(compiler.As(&versionInfo2)))
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

	ShaderCompilerIdentity QueryDxcCompilerIdentity() noexcept
	{
		ShaderCompilerIdentity identity{};
		ComPtr<IDxcCompiler3> compiler;
		if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
		{
			identity.m_CanonicalIdentity = L"unknown";
			return identity;
		}
		identity.m_CanonicalIdentity = QueryDxcVersionFrom(compiler);
		return identity;
	}
}

#undef GGLAB_LOG_SHADER_COMPILER
