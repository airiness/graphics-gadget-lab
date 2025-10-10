#pragma once
#include "TypedIndex.h"
#include "PSOKey.h"
#include "FNV1a.h"
#include "GraphicsTypes.h"
#include "Shader.h"

namespace gglab
{
	struct ShaderKey
	{
		ShaderHash128 m_Key;
		auto AsTuple() const noexcept { return m_Key.AsTuple(); }
		constexpr bool operator==(const ShaderKey&) const noexcept = default;
	};
	using ShaderKeyHash = KeyHash<ShaderKey>;

	class ShaderCompiler;
	class ShaderManager
	{
	public:
		ShaderManager() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderManager);
		~ShaderManager() = default;


		ShaderId LoadShader(const ShaderDesc& desc) noexcept;
		void Preload(const std::vector<std::pair<std::filesystem::path, ShaderStage>>& shaders) noexcept;
		int32_t ReloadChanged() noexcept;

		D3D12_SHADER_BYTECODE GetBytecode(ShaderId shaderId) const noexcept;
		ShaderBlob* GetBlob(ShaderId shaderId) const noexcept;
		ShaderHash128 GetHash(ShaderId shaderId) const noexcept;
		uint64_t GetGeneration(ShaderId shaderId) const noexcept;

		bool LinkOrMirrorCacheToOutDir(const std::filesystem::path& outDirShaders) noexcept;

	private:
		static std::wstring DefaultEntry(ShaderStage& stage) noexcept;
		static bool GetDxilContainerHash(const void* data, size_t size, ShaderHash128& outHash) noexcept;
		static ShaderHash128 HashBlob(ShaderBlob* blob) noexcept;
		static ShaderDesc MergeDefaultShaderDesc(const ShaderDesc& desc) noexcept;

	private:
		mutable std::shared_mutex m_Mutex;
		std::unordered_map<ShaderKey, ShaderId, ShaderKeyHash> m_KeyIdMap;
		std::vector<std::unique_ptr<Shader>> m_Shaders;

		std::unique_ptr<ShaderCompiler> m_Compiler;
	};
}