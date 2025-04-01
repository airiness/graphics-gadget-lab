#include "Precompiled.h"
#include "Renderer.h"

namespace graphicsGadgetLab
{
	Renderer::Renderer() noexcept :
		m_Device(),
		m_DirectCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT),
		m_ComputeCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE),
		m_CopyCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY)
	{
	}

	Renderer::~Renderer() noexcept
	{
	}

	void Renderer::Initialize() noexcept
	{
	}

	void Renderer::Update() noexcept
	{
	}

	void Renderer::Finalize() noexcept
	{
	}
}