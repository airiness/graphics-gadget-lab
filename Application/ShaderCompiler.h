#pragma once
#include "Shader.h"
#include "PathUtils.h"

namespace gglab
{
	class ShaderIncludeHandler final
		: public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDxcIncludeHandler>
	{
	public:
		HRESULT Initialize(ComPtr<IDxcUtils> utils, const std::vector<std::filesystem::path>& includeDirs) noexcept
		{
			m_Utils = utils;
			m_IncludeDirs = includeDirs;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE
			LoadSource(_In_z_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource) override
		{
			const auto path = utils::Canonical(pFilename);
			ComPtr<IDxcBlobEncoding> blob;

			if (SUCCEEDED(m_Utils->LoadFile(path.c_str(), nullptr, &blob)))
			{
				m_IncludeDirs.push_back(path);
				*ppIncludeSource = blob.Detach();
				return S_OK;
			}

			for (auto& dir : m_IncludeDirs)
			{
				const auto pathInDir = utils::Canonical(dir / pFilename);
				if(SUCCEEDED(m_Utils->LoadFile(pathInDir.c_str(), nullptr, &blob)))
				{
					m_Includes.push_back(pathInDir);
					*ppIncludeSource = blob.Detach();
					return S_OK;
				}
			}

			*ppIncludeSource = nullptr;
			return E_FAIL;
		}

		const std::vector<std::filesystem::path>& Includes() const noexcept { return m_Includes; }

	private:
		ComPtr<IDxcUtils> m_Utils;

		std::vector<std::filesystem::path> m_IncludeDirs;
		std::vector<std::filesystem::path> m_Includes;

	};

	// TODO: User Shader directly
	struct ShaderCompileResult
	{
		std::filesystem::path m_DxilPath;
		std::filesystem::path m_MetaPath;

		ComPtr<ShaderBlob> m_DxilBlob;
		bool m_FromCache = false;
	};

	class ShaderCompiler
	{
	public:
		
		ShaderCompiler() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderCompiler);
		~ShaderCompiler() = default;

		void SetCacheRootDirectory(std::filesystem::path root) noexcept;
		const std::filesystem::path& GetCacheRootDirectory() const noexcept { return m_CacheRootDir; }

		// TODO: Rename this function
		ShaderCompileResult EnsureCompiled(const ShaderDesc& desc) noexcept;

		bool LinkOrMirrorToOutDir(const std::filesystem::path& outDirShaders) noexcept;

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

	private:
		std::filesystem::path m_CacheRootDir;
		ComPtr<IDxcUtils> m_Utils;
		ComPtr<IDxcCompiler3> m_Compiler;
	};
}