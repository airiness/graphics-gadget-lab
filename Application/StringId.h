#pragma once

namespace gglab
{
	constexpr uint64_t CRC64_POLY = 0x42F0E1EBA9EA3693ull;

	constexpr uint64_t Crc64TableEntry(uint64_t index)
	{
		uint64_t r = index;
		for (int32_t i = 0; i < 8; ++i)
		{
			r = (r & 1) ? (r >> 1) ^ CRC64_POLY : (r >> 1);
		}
		return r;
	}

	constexpr auto CRC64_TABLE = []
		{
			return[]<size_t... I>(std::index_sequence<I...>)
			{
				return std::array<uint64_t, sizeof...(I)>{ Crc64TableEntry(I)... };
			}(std::make_index_sequence<256>{});
		}();

	constexpr uint64_t Crc64Bytes(const unsigned char* data, size_t size) noexcept
	{
		uint64_t crc = std::numeric_limits<uint64_t>::max();
		for (size_t i = 0; i < size; ++i)
		{
			crc = CRC64_TABLE[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
		}
		return crc ^ std::numeric_limits<uint64_t>::max();
	}

	constexpr uint64_t Crc64(std::string_view stringView)
	{
		return Crc64Bytes(reinterpret_cast<const unsigned char*>(stringView.data()), stringView.size());
	}

	constexpr uint64_t Crc64(std::wstring_view wideStringView)
	{
		return Crc64Bytes(reinterpret_cast<const unsigned char*>(wideStringView.data()), wideStringView.size() * sizeof(wchar_t));
	}

#if defined(BUILD_DEBUG)
	class StringIdRegistry
	{
	public:
		void RegisterName(uint64_t hash, std::string_view stringView)
		{
			// Use unique_lock when write.
			std::unique_lock lock(m_Mutex);

			// TODO: need log here?
			/*if (auto iter = m_Dictionary.find(hash); iter != m_Dictionary.end())
			{
				GGLAB_LOG_INFO("Hash collision: {}<->{}.", iter->second.c_str(), stringView.data());
			}*/
			m_Dictionary.emplace(hash, std::string(stringView));
		}

		std::string_view Lookup(uint64_t hash) const noexcept
		{
			// Use shared_lock when read.
			std::shared_lock lock(m_Mutex);

			if (auto iter = m_Dictionary.find(hash); iter != m_Dictionary.end())
			{
				return iter->second;
			}

			static const std::string EmptyStr;
			return EmptyStr;
		}

	private:
		static StringIdRegistry& Instance() noexcept
		{
			static StringIdRegistry instance;
			return instance;
		}

		StringIdRegistry() noexcept = default;
		~StringIdRegistry() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(StringIdRegistry);

		friend class StringID;

	private:
		mutable std::shared_mutex m_Mutex;
		std::unordered_map<uint64_t, std::string> m_Dictionary;
	};

#endif

	class StringID
	{
	public:
		constexpr explicit StringID(uint64_t v) noexcept :
			m_Value(v)
		{
		}
		constexpr StringID() = default;

		explicit StringID(std::string_view stringView) noexcept :
			m_Value(Crc64(stringView))
		{
#if defined(BUILD_DEBUG)
			StringIdRegistry::Instance().RegisterName(m_Value, stringView);
#endif
		}

		uint64_t Value() const noexcept
		{
			return m_Value;
		}

		constexpr auto operator<=>(const StringID&) const = default;

#if defined(BUILD_DEBUG)
		[[nodiscard]] std::string_view DebugString() const
		{
			return StringIdRegistry::Instance().Lookup(m_Value);
		}
#endif

	private:
		uint64_t m_Value = 0;
	};

	// Generate Id in compile time
	consteval StringID operator"" _id(const char* str, size_t len)
	{
		return StringID{ Crc64(std::string_view{ str, len }) };
	}
}

// StringID hash for using as a key
namespace std
{
	template<> struct hash<gglab::StringID>
	{
		size_t operator()(const gglab::StringID& id) const noexcept
		{
			return std::hash<uint64_t>{}(id.Value());
		}
	};
}