#pragma once
#include <wrl.h>

namespace gglab
{
	template<typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
}
