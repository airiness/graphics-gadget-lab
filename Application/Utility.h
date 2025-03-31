#pragma once
namespace graphicsGadgetLab 
{
	namespace utility 
	{
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
	};
}


