#pragma once
namespace graphicsGadgetLab 
{
	// TODO: Refactor this file
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

		// Get the name of a command list type
		constexpr const std::wstring GetCommandListTypeName(D3D12_COMMAND_LIST_TYPE type)
		{
			switch (type)
			{
			case D3D12_COMMAND_LIST_TYPE_DIRECT:		return L"Direct";
			case D3D12_COMMAND_LIST_TYPE_COMPUTE:		return L"Compute";
			case D3D12_COMMAND_LIST_TYPE_COPY:			return L"Copy";
			case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:  return L"VideoDecode";
			case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS: return L"VideoProcess";
			default: return L"Unknown";
			}
		}

		inline void SetDebugName(ID3D12Object* object, LPCWSTR name) 
		{
			if (object) 
			{
				object->SetName(name);
			}
		}
	};
}


