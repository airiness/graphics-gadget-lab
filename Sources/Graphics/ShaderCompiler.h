#pragma once
#include "Graphics/Shader.h"

namespace gglab
{
	class ShaderCompiler
	{
	public:
		ShaderCompiler() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderCompiler);
		~ShaderCompiler();

		void SetCacheRootDirectory(std::filesystem::path root) noexcept;
		const std::filesystem::path& GetCacheRootDirectory() const noexcept { return m_CacheRootDir; }
		void SetDefaultShaderConfig(const ShaderDesc& defaultShaderConfig) noexcept { m_DefaultShaderConfig = defaultShaderConfig; }
		const ShaderDesc& GetDefaultShaderConfig() const noexcept { return m_DefaultShaderConfig; }

		ShaderCompileArtifact CompileOrLoadArtifact(const ShaderDesc& desc) noexcept;

		ShaderDesc NormalizeShaderDesc(const ShaderDesc& userDesc) const noexcept;

	public:
		static ShaderHash128 ComputeRecipeHash(const ShaderDesc& mergedDesc) noexcept;

	private:
		std::filesystem::path MakeCacheBinaryPath(const std::wstring& keyHex, ShaderStage stage) const noexcept;
		ShaderBinary CompileShader(const ShaderDesc& desc,
			std::vector<std::filesystem::path>& outDeps) const noexcept;
		void WriteMeta(const std::filesystem::path& meta, const ShaderDesc& desc,
			const std::vector<std::filesystem::path>& deps) const noexcept;
		bool IsMetaUpToDate(const std::filesystem::path& meta) const noexcept;

	private:
		static std::wstring DefaultEntry(const ShaderStage& stage) noexcept;
		static std::wstring ToHex(ShaderHash128 hash) noexcept;
		static std::wstring ToTarget(ShaderStage stage, ShaderModel model) noexcept;
		static std::wstring BuildKeyString(const ShaderDesc& desc) noexcept;
		static bool GetContainerHash(const void* data, size_t size, ShaderHash128& outHash) noexcept;
		static ShaderHash128 ComputeHashFromBinary(const ShaderBinary& binary) noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
		std::filesystem::path m_CacheRootDir;
		ShaderDesc m_DefaultShaderConfig;
	};
}
