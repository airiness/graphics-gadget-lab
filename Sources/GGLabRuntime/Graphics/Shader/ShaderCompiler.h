#pragma once
#include "Contracts/ShaderCompileTarget.h"
#include "Contracts/ShaderCompileTypes.h"
#include "Graphics/Shader/Shader.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gglab
{
	[[nodiscard]] ShaderCompileValidationResult ValidateShaderDesc(
		const ShaderDesc& desc, const ShaderCompilerIdentity& compilerIdentity) noexcept;

	class ShaderCompiler
	{
	public:
		ShaderCompiler(std::filesystem::path sourceRoot, std::filesystem::path cacheRoot) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderCompiler);
		~ShaderCompiler();

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

		ShaderCompileArtifact CompileOrLoadArtifact(const ShaderDesc& desc) noexcept;

		ShaderDesc NormalizeShaderDesc(const ShaderDesc& userDesc) const noexcept;

	public:
		static ShaderHash128 ComputeRecipeHash(
			const ShaderDesc& mergedDesc, const ShaderCompilerIdentity& compilerIdentity) noexcept;
		static std::vector<std::wstring> BuildCompileArguments(const ShaderDesc& desc) noexcept;

	private:
		std::filesystem::path MakeCacheBinaryPath(
			const std::wstring& keyHex, ShaderStage stage, ShaderBinaryFormat format) const noexcept;
		ShaderBinary CompileShader(
			const ShaderDesc& desc, std::vector<std::filesystem::path>& outDeps) const noexcept;
		void WriteMeta(const std::filesystem::path& meta, const ShaderDesc& desc,
			const std::vector<std::filesystem::path>& deps, ShaderHash128 recipeHash,
			const ShaderCompilerIdentity& compilerIdentity) const noexcept;
		bool IsMetaUpToDate(const std::filesystem::path& meta, const ShaderDesc& desc,
			ShaderHash128 recipeHash, const ShaderCompilerIdentity& compilerIdentity) const noexcept;

	private:
		static std::wstring DefaultEntry(const ShaderStage& stage) noexcept;
		static std::wstring ToHex(ShaderHash128 hash) noexcept;
		static std::wstring ToTarget(ShaderStage stage, ShaderModel model) noexcept;
		std::wstring QueryDxcVersion() const noexcept;
		static bool GetContainerHash(
			const void* data, size_t size, ShaderHash128& outHash) noexcept;
		static bool IsBinaryFormat(
			const ShaderBinary& binary, ShaderBinaryFormat format) noexcept;
		static ShaderHash128 ComputeHashFromBinary(
			const ShaderBinary& binary, ShaderBinaryFormat format) noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
		std::filesystem::path m_SourceRootDir;
		std::filesystem::path m_CacheRootDir;
		ShaderDesc m_DefaultShaderConfig;
		ShaderCompilerIdentity m_CompilerIdentity;
	};
}
