#pragma once
#include "Shader.h"

namespace gglab
{
	class ShaderCompiler
	{
	public:	
		ShaderCompiler() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderCompiler);
		~ShaderCompiler() = default;

		ShaderCompileArtifact CompileOrLoadArtifact(const ShaderDesc& desc) noexcept;
		void SetCacheRootDirectory(std::filesystem::path root) noexcept;
		const std::filesystem::path& GetCacheRootDirectory() const noexcept { return m_CacheRootDir; }
		bool LinkOrMirrorToOutDir(const std::filesystem::path& outDirShaders) noexcept;

	public:
		static ShaderHash128 ComputeRecipeHash(const ShaderDesc& mergedDesc) noexcept;

	private:
		std::filesystem::path MakeCacheDxilPath(const std::wstring& keyHex, ShaderStage stage) const noexcept;
		ComPtr<IDxcBlob> CompileShader(const ShaderDesc& desc, std::vector<std::filesystem::path>& outDeps) const noexcept;
		void WriteMeta(const std::filesystem::path& meta, const ShaderDesc& desc,
			const std::vector<std::filesystem::path>& deps) const noexcept;
		bool IsMetaUpToDate(const std::filesystem::path& meta) const noexcept;

		bool LinkOrCopyDir(const std::filesystem::path& src, const std::filesystem::path& dst) const noexcept;

		static std::wstring ToTarget(ShaderStage stage, ShaderModel model) noexcept;
		static std::wstring BuildKeyString(const ShaderDesc& desc) noexcept;
		static std::wstring HashKeyString(std::wstring_view keyStr) noexcept;

		static ShaderHash128 ComputeDxilHasFromBlob(IDxcBlob* blob) noexcept;

	private:
		std::filesystem::path m_CacheRootDir;
		ComPtr<IDxcUtils> m_Utils;
		ComPtr<IDxcCompiler3> m_Compiler;
	};
}