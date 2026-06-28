#pragma once
#include "DevTools/EnumText/EnumText.h"
#include "Graphics/RHI/DX12/DX12QueueSystem.h"
#include <d3d12.h>

namespace gglab::devtools
{
	template<>
	struct EnumTextTraits<DX12QueueType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ DX12QueueType::Graphics, "Graphics" },
			EnumTextEntry{ DX12QueueType::Compute, "Compute" },
			EnumTextEntry{ DX12QueueType::Copy, "Copy" },
			EnumTextEntry{ DX12QueueType::Transfer, "Transfer" },
		};
	};

	template<>
	struct EnumTextTraits<D3D12_BARRIER_SYNC>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ D3D12_BARRIER_SYNC_NONE, "None" },
			EnumTextEntry{ D3D12_BARRIER_SYNC_ALL, "All" },
			EnumTextEntry{ D3D12_BARRIER_SYNC_ALL_SHADING, "AllShading" },
			EnumTextEntry{ D3D12_BARRIER_SYNC_RENDER_TARGET, "RenderTarget" },
			EnumTextEntry{ D3D12_BARRIER_SYNC_DEPTH_STENCIL, "DepthStencil" },
			EnumTextEntry{ D3D12_BARRIER_SYNC_COPY, "Copy" },
			EnumTextEntry{ D3D12_BARRIER_SYNC_VERTEX_SHADING, "VertexShading" },
			EnumTextEntry{ D3D12_BARRIER_SYNC_INDEX_INPUT, "IndexInput" },
		};
		static constexpr std::string_view NoneText = "None";
	};

	template<>
	struct EnumTextTraits<D3D12_BARRIER_ACCESS>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ D3D12_BARRIER_ACCESS_COMMON, "Common" },
			EnumTextEntry{ D3D12_BARRIER_ACCESS_SHADER_RESOURCE, "ShaderResource" },
			EnumTextEntry{ D3D12_BARRIER_ACCESS_RENDER_TARGET, "RenderTarget" },
			EnumTextEntry{ D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE, "DepthStencilWrite" },
			EnumTextEntry{ D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ, "DepthStencilRead" },
			EnumTextEntry{ D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, "UnorderedAccess" },
			EnumTextEntry{ D3D12_BARRIER_ACCESS_COPY_SOURCE, "CopySource" },
			EnumTextEntry{ D3D12_BARRIER_ACCESS_COPY_DEST, "CopyDest" },
			EnumTextEntry{ D3D12_BARRIER_ACCESS_VERTEX_BUFFER, "VertexBuffer" },
			EnumTextEntry{ D3D12_BARRIER_ACCESS_INDEX_BUFFER, "IndexBuffer" },
			EnumTextEntry{ D3D12_BARRIER_ACCESS_CONSTANT_BUFFER, "ConstantBuffer" },
		};
		static constexpr std::string_view NoneText = "Common";
	};

	template<>
	struct EnumTextTraits<D3D12_BARRIER_LAYOUT>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ D3D12_BARRIER_LAYOUT_COMMON, "Common" },
			EnumTextEntry{ D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, "ShaderResource" },
			EnumTextEntry{ D3D12_BARRIER_LAYOUT_RENDER_TARGET, "RenderTarget" },
			EnumTextEntry{ D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, "DepthStencilWrite" },
			EnumTextEntry{ D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ, "DepthStencilRead" },
			EnumTextEntry{ D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS, "UnorderedAccess" },
			EnumTextEntry{ D3D12_BARRIER_LAYOUT_COPY_SOURCE, "CopySource" },
			EnumTextEntry{ D3D12_BARRIER_LAYOUT_COPY_DEST, "CopyDest" },
			EnumTextEntry{ D3D12_BARRIER_LAYOUT_PRESENT, "Present" },
		};
	};
}
