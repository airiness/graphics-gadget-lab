#pragma once
#include "TypedIndex.h"
#include "StringId.h"
#include "PSOKey.h"
#include "FNV1a.h"
#include "GraphicsTypes.h"
#include "Shader.h"
#include "ShaderCompiler.h"

namespace gglab
{
	// DXC shader blob
	//using ShaderBlob = IDxcBlobEncoding;

	// D3D shader blob
	// using ShaderBlob = ID3DBlob;


	struct ShaderKey
	{
		StringId m_PathHash{};
		ShaderStage m_Stage = ShaderStage::Vertex;
		uint64_t m_EntryHash = 0;
		uint64_t m_TargetHash = 0;

		bool operator==(const ShaderKey&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_PathHash.Value(), m_Stage, m_EntryHash, m_TargetHash);
		}
	};
	using ShaderKeyHash = KeyHash<ShaderKey>;

	//struct ShaderRecord
	//{
	//	ShaderKey m_Key{};
	//	std::filesystem::path m_Path{};
	//	ShaderStage m_Stage = ShaderStage::Vertex;
	//	ComPtr<ShaderBlob> m_Blob;
	//	ShaderHash128 m_Hash{};
	//	std::filesystem::file_time_type m_Timestamp{};
	//	uint64_t m_Generation = 1;
	//};

	class ShaderManager
	{
	public:
		ShaderManager() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderManager);
		~ShaderManager() = default;

		ShaderId LoadShader(const std::filesystem::path& path, ShaderStage stage) noexcept;
		D3D12_SHADER_BYTECODE GetBytecode(ShaderId shaderId) const noexcept;
		ShaderBlob* GetBlob(ShaderId shaderId) const noexcept;
		ShaderHash128 GetHash(ShaderId shaderId) const noexcept;
		uint64_t GetGeneration(ShaderId shaderId) const noexcept;

		void Preload(const std::vector<std::pair<std::filesystem::path, ShaderStage>>& shaders) noexcept;
		int32_t ReloadChanged() noexcept;	// TODO: Reload tick

	private:
		static bool GetDxilContainerHash(const void* data, size_t size, ShaderHash128& outHash) noexcept;
		static ShaderHash128 HashBlob(ShaderBlob* blob) noexcept;

	private:
		mutable std::shared_mutex m_Mutex;
		std::unordered_map<ShaderKey, ShaderId, ShaderKeyHash> m_KeyIdMap;
		std::vector<std::unique_ptr<Shader>> m_Shaders;

		std::unique_ptr<ShaderCompiler> m_Compiler;

		/*ComPtr<IDxcUtils> m_DxcUtils;*/
	};
}