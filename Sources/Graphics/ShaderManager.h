#pragma once
#include "Core/TypedIndex.h"
#include "Graphics/RHI/DX12/Cache/PSOKey.h"
#include "Core/Hash/FNV1a.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Shader.h"
#include "Graphics/ShaderCompiler.h"

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

		ShaderID LoadShader(const ShaderDesc& desc) noexcept;
		void Preload(const std::vector<ShaderDesc>& descList) noexcept;
		
		int32_t RefreshChanged() noexcept;
		bool RefreshShader(ShaderID shaderId) noexcept;
		D3D12_SHADER_BYTECODE GetBytecode(ShaderID shaderId) const noexcept;
		ShaderBlob* GetBlob(ShaderID shaderId) const noexcept;
		ShaderHash128 GetHash(ShaderID shaderId) const noexcept;
		uint64_t GetGeneration(ShaderID shaderId) const noexcept;
		uint64_t GetRevision() const noexcept
		{
			return m_Revision.load(std::memory_order_relaxed);
		}

	private:
		bool RefreshShaderInternal(Shader& shader) noexcept;
		bool RefreshShaderInternal(Shader& shader, const ShaderDesc& normalizedDesc) noexcept;

	private:
		mutable std::shared_mutex m_Mutex;
		std::unordered_map<ShaderKey, ShaderID, ShaderKeyHash> m_KeyIdMap;
		std::vector<std::unique_ptr<Shader>> m_Shaders;

		std::unique_ptr<ShaderCompiler> m_Compiler;
		std::atomic_uint64_t m_Revision = 1;
	};
}
