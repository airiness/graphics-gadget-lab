#include "Precompiled.h"
#include "InputLayoutLibrary.h"
#include "VertexData.h"

namespace gglab
{
	namespace
	{
		static const D3D12_INPUT_ELEMENT_DESC P3Elements[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
		static const D3D12_INPUT_ELEMENT_DESC P3T2Elements[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
		static const D3D12_INPUT_ELEMENT_DESC P3N3Elements[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
		static const D3D12_INPUT_ELEMENT_DESC P3N3T2Elements[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		static const D3D12_INPUT_LAYOUT_DESC InputLayouts[static_cast<size_t>(InputLayoutId::Count)] =
		{
			// P3
			{ P3Elements, _countof(P3Elements) },
			// P3T2
			{ P3T2Elements, _countof(P3T2Elements) },
			// P3N3
			{ P3N3Elements, _countof(P3N3Elements) },
			// P3N3T2
			{ P3N3T2Elements, _countof(P3N3T2Elements) },
		};
	}

	D3D12_INPUT_LAYOUT_DESC InputLayoutLibrary::Get(InputLayoutId id) noexcept
	{
		if (id >= InputLayoutId::None)
		{
			return D3D12_INPUT_LAYOUT_DESC{ nullptr, 0 };
		}

		const auto index = static_cast<size_t>(id);
		return InputLayouts[index];
	}
}