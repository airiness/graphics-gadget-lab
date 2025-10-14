#pragma once
#include "TypedIndex.h"
#include "PSOKey.h"
#include "FNV1a.h"
#include "GraphicsTypes.h"
#include "Shader.h"
#include "ShaderCompiler.h"

namespace gglab
{
	struct ShaderKey
	{
		ShaderHash128 m_KeyHash;
		auto AsTuple() const noexcept { return m_KeyHash.AsTuple(); }
		constexpr bool operator==(const ShaderKey&) const noexcept = default;
	};
	using ShaderKeyHash = KeyHash<ShaderKey>;

	class ShaderManager
	{
	public:
		ShaderManager() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderManager);
		~ShaderManager() = default;

		void SetDefaultShaderConfig(const ShaderDesc& defaultDesc) noexcept;

		ShaderId LoadShader(const ShaderDesc& desc) noexcept;
		void Preload(const std::vector<ShaderDesc>& descList) noexcept;
		
		int32_t RefreshChanged() noexcept;
		bool RefreshShader(ShaderId shaderId) noexcept;
		D3D12_SHADER_BYTECODE GetBytecode(ShaderId shaderId) const noexcept;
		ShaderBlob* GetBlob(ShaderId shaderId) const noexcept;
		ShaderHash128 GetHash(ShaderId shaderId) const noexcept;
		uint64_t GetGeneration(ShaderId shaderId) const noexcept;

	private:
		bool RefreshShaderInternal(Shader& shader) noexcept;
		bool RefreshShaderInternal(Shader& shader, const ShaderDesc& normalizedDesc) noexcept;

	private:
		mutable std::shared_mutex m_Mutex;
		std::unordered_map<ShaderKey, ShaderId, ShaderKeyHash> m_KeyIdMap;
		std::vector<std::unique_ptr<Shader>> m_Shaders;

		std::unique_ptr<ShaderCompiler> m_Compiler;
	};
}