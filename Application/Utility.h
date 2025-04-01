#pragma once
namespace graphicsGadgetLab 
{
	namespace utility 
	{
		// Throw a runtime_error exception with the given message
		inline std::string HrToString(HRESULT hr)
		{
			char s_str[64] = {};
			sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
			return std::string(s_str);
		}

		class HrException : public std::runtime_error
		{
		public:
			HrException(HRESULT hr) : std::runtime_error(HrToString(hr)), m_Hr(hr) {}
			HRESULT Error() const { return m_Hr; }
		private:
			const HRESULT m_Hr;
		};

		inline void ThrowIfFailed(HRESULT hr)
		{
			if (FAILED(hr))
			{
				throw HrException(hr);
			}
		}

		// Assert process
#ifdef BUILD_DEBUG
#define ASSERT_MSG(condition, ...) \
        if (!(condition)) { \
            fprintf(stderr, "[ASSERT] %s:%d | %s\n  Failed: %s\n  Message: ", \
                __FILE__, __LINE__, __func__, #condition); \
            fprintf(stderr, __VA_ARGS__); \
            fputc('\n', stderr); \
            fflush(stderr); \
            std::abort(); \
        }
#else
#define ASSERT_MSG(condition, ...) ((void)0)
#endif

		// Get the name of a command list type
		constexpr const char* GetCommandListTypeName(D3D12_COMMAND_LIST_TYPE type)
		{
			switch (type)
			{
			case D3D12_COMMAND_LIST_TYPE_DIRECT:		return "Direct";
			case D3D12_COMMAND_LIST_TYPE_COMPUTE:		return "Compute";
			case D3D12_COMMAND_LIST_TYPE_COPY:			return "Copy";
			case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:  return "VideoDecode";
			case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS: return "VideoProcess";
			default: return "Unknown";
			}
		}

		void SetDebugName(ID3D12Object* object, LPCWSTR name) 
		{
#if defined(BUILD_DEBUG)
			if (object) 
			{
				object->SetName(name);
			}
#endif
		}
	};
}


