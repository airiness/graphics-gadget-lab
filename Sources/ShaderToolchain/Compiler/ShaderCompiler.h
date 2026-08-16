#pragma once
#include "Contracts/ShaderArtifact.h"
#include "Contracts/ShaderArtifactManifest.h"
#include "Contracts/ShaderCompileTarget.h"
#include "Contracts/ShaderCompileTypes.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gglab
{
	// Producer identity of the active DXC installation, independent of any
	// compiler instance (used by CLI --version and CI diagnostics).
	[[nodiscard]] ShaderCompilerIdentity QueryDxcCompilerIdentity() noexcept;

	// Host-side in-process shader compiler (transitional producer while the
	// runtime keeps in-process compilation).
	// Instances are thread-confined; multiple instances may share one cache
	// root because cache publication follows the cross-thread/cross-process
	// manifest-last protocol instead of process-local locking.
	class ShaderCompiler
	{
	public:
		ShaderCompiler(std::filesystem::path sourceRoot, std::filesystem::path cacheRoot) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderCompiler);
		~ShaderCompiler();

		// Test seam: a compiler whose DXC initialization is recorded as failed
		// so CompilerUnavailable behavior can be verified deterministically.
		[[nodiscard]] static std::unique_ptr<ShaderCompiler> MakeUnavailable(
			std::filesystem::path sourceRoot, std::filesystem::path cacheRoot) noexcept;

		void SetSourceRootDirectory(std::filesystem::path root) noexcept;
		const std::filesystem::path& GetSourceRootDirectory() const noexcept
		{
			return m_SourceRootDir;
		}
		void SetCacheRootDirectory(std::filesystem::path root) noexcept;
		const std::filesystem::path& GetCacheRootDirectory() const noexcept
		{
			return m_CacheRootDir;
		}
		void SetDefaultShaderConfig(const ShaderDesc& defaultShaderConfig) noexcept
		{
			m_DefaultShaderConfig = defaultShaderConfig;
		}
		const ShaderDesc& GetDefaultShaderConfig() const noexcept { return m_DefaultShaderConfig; }
		const ShaderCompilerIdentity& GetCompilerIdentity() const noexcept
		{
			return m_CompilerIdentity;
		}

		// Normalizes and validates the request and derives the recipe/build
		// identity. The resolved recipe is the single compile authority.
		[[nodiscard]] ShaderResolvedRecipe Resolve(const ShaderDesc& request) noexcept;

		// Compiles or loads the resolved recipe through the shared cache. The
		// recipe is re-validated against the executing compiler first: a
		// recipe from another producer, or a mutated recipe, is rejected with
		// a structured result instead of being re-interpreted.
		// Compile failures are structured results, never fatal.
		[[nodiscard]] ShaderCompileResult CompileOrLoad(
			const ShaderResolvedRecipe& recipe) noexcept;

		// Convenience: Resolve + CompileOrLoad.
		[[nodiscard]] ShaderCompileResult Compile(const ShaderDesc& request) noexcept;

	public:
		// Contract observability for tests and diagnostics.
		[[nodiscard]] static std::vector<std::wstring> BuildCompileArguments(
			const ShaderDesc& desc) noexcept;
		[[nodiscard]] static LocalShaderCacheKey ComputeBuildKey(
			const ShaderRecipeId& recipeId,
			const ShaderCompilerIdentity& compilerIdentity) noexcept;
		[[nodiscard]] std::filesystem::path GetCacheBinaryPath(
			const ShaderResolvedRecipe& recipe) const noexcept;

	private:
		struct Impl;

		[[nodiscard]] bool IsCompilerAvailable() const noexcept;
		[[nodiscard]] ShaderCompilerDiagnostics MakeDiagnostics(
			ShaderCompileStatus status, std::wstring message,
			ShaderCompileValidationError validationError =
				ShaderCompileValidationError::None) const noexcept;
		[[nodiscard]] ShaderResolvedRecipe ResolveRecipe(const ShaderDesc& request) noexcept;
		[[nodiscard]] ShaderCompileResult CompileOrLoadInternal(
			const ShaderResolvedRecipe& recipe) noexcept;
		[[nodiscard]] bool ValidateRecipeAuthority(
			const ShaderResolvedRecipe& recipe, ShaderCompilerDiagnostics& outDiagnostics) const noexcept;
		[[nodiscard]] std::filesystem::path MakeCacheBinaryPath(
			const std::wstring& keyHex, ShaderStage stage, ShaderBinaryFormat format) const noexcept;
		[[nodiscard]] ShaderArtifactCacheRecord BuildCacheRecord(
			const ShaderResolvedRecipe& recipe, const ShaderBinary& binary,
			const std::vector<ShaderArtifactDependency>& dependencies) const noexcept;
		[[nodiscard]] bool ValidateCacheRecordAgainstRecipe(
			const ShaderArtifactCacheRecord& record, const ShaderResolvedRecipe& recipe) const noexcept;

		[[nodiscard]] ShaderBinary CompileShaderBinary(const ShaderResolvedRecipe& recipe,
			std::vector<ShaderArtifactDependency>& outDependencies,
			ShaderCompilerDiagnostics& outDiagnostics) const noexcept;

	private:
		static std::wstring DefaultEntry(const ShaderStage& stage) noexcept;
		static std::wstring ToTarget(ShaderStage stage, ShaderModel model) noexcept;
		static ShaderRecipeId ComputeRecipeId(
			const std::filesystem::path& logicalSourcePath,
			const std::vector<std::filesystem::path>& logicalIncludeDirs,
			const ShaderDesc& mergedDesc) noexcept;
		std::wstring QueryDxcVersion() const noexcept;

	private:
		std::unique_ptr<Impl> m_Impl;
		std::filesystem::path m_SourceRootDir;
		std::filesystem::path m_CacheRootDir;
		ShaderDesc m_DefaultShaderConfig;
		ShaderCompilerIdentity m_CompilerIdentity;
	};
}
