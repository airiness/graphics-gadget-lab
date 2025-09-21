#pragma once
#include "TypedIndex.h"
#include "StringId.h"
#include "PSOKey.h"
#include "FNV1a.h"

namespace gglab
{
	GGLAB_DEFINE_TYPED_INDEX(ShaderId, uint32_t);

	// DXC shader blob
	using ShaderBlob = IDxcBlobEncoding;

	// D3D shader blob
	// using ShaderBlob = ID3DBlob;

	enum class ShaderStage
	{
		Vertex,
		Pixel,
		Hull,
		Domain,
		Geometry,
		Copute
	};

	struct ShaderKey
	{
		StringId m_PathHash{};
		ShaderStage m_Stage = ShaderStage::Vertex;
		uint64_t m_EntryHash = 0;
		uint64_t m_TargetHash = 0;

		bool operator==(const ShaderKey&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_PathHash, m_Stage, m_EntryHash, m_TargetHash);
		}
	};
	using ShaderKeyHash = KeyHash<ShaderKey>;

	struct ShaderRecord
	{
		ShaderKey m_Key{};
		std::filesystem::path m_Path{};
		ShaderStage m_Stage = ShaderStage::Vertex;
		ComPtr<ShaderBlob> m_Blob;
		ShaderHash128 m_Hash{};
		std::filesystem::file_time_type m_Timestamp{};
		uint64_t m_Generation = 1;
	};

	class ShaderManager
	{
	public:
		ShaderManager() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderManager);
		~ShaderManager() = default;

		ShaderId LoadShader(const std::filesystem::path& path, ShaderStage stage) noexcept;
		D3D12_SHADER_BYTECODE GetBytecode(ShaderId shaderId) const noexcept;
		const ComPtr<ShaderBlob>& GetBlob(ShaderId shaderId) const noexcept;
		ShaderHash128 GetHash(ShaderId shaderId) const noexcept;
		uint64_t GetGeneration(ShaderId shaderId) const noexcept;

		void Preload(const std::vector<std::pair<std::filesystem::path, ShaderStage>>& shaders) noexcept;
		int32_t ReloadChanged() noexcept;

	private:
		ShaderId InsertOrGet(const ShaderKey& key, ComPtr<ShaderBlob>&& blob, ShaderStage stage) noexcept;

	private:
		static bool GetDxilValidatorHash(const void* data, size_t size, ShaderHash128& outHash) noexcept;
		static ShaderHash128 HashBlob(ShaderBlob* blob) noexcept;

	private:
		mutable std::shared_mutex m_Mutex;
		std::unordered_map<ShaderKey, ShaderId, ShaderKeyHash> m_KeyIdMap;
		std::vector<ShaderRecord> m_Records;

		ComPtr<IDxcUtils> m_DxcUtils;
	};
}