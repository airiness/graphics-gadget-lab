#include "Precompiled.h"
#include "Renderer.h"
#include "Application.h"
#include "DX12Device.h"
#include "DX12RootSignature.h"
#include "DX12PipelineState.h"

namespace graphicsGadgetLab
{
	Renderer::Renderer() noexcept
	{
		auto width = Application::Get()->GetWindowWidth();
		auto height = Application::Get()->GetWindowHeight();

		m_Device = std::make_unique<DX12Device>();
		m_Device->Initialize();
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

	void Renderer::Render() noexcept
	{
	}

	void Renderer::Finalize() noexcept
	{
	}

	void Renderer::InitializeRootSignatures() noexcept
	{
		// Common RootSignature
		{
			CD3DX12_ROOT_PARAMETER1 rootParameters[4];
		}

	}

	void Renderer::InitializePipelineStates() noexcept
	{
	}
}