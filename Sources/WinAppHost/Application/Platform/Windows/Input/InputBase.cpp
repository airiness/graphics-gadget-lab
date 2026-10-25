#include "Application/Platform/Windows/Input/InputBase.h"
#include "AppRuntimeLog.h"
#include "GGLabFoundation/Platform/Win/HResult.h"

namespace gglab
{
	InputBase::InputBase(IGameInput* gameInput, GameInputKind inputKind) noexcept :
		m_GameInput(gameInput)
	{
		if (!m_GameInput)
		{
			return;
		}

		const HRESULT result = m_GameInput->RegisterDeviceCallback(
			nullptr,
			inputKind,
			GameInputDeviceConnected,
			GameInputBlockingEnumeration,
			this,
			OnGameInputDevice,
			&m_CallbackToken);
		if (FAILED(result))
		{
			GGLAB_LOG_ERROR("Failed to register a GameInput device callback: {}",
				FormatHResult(result));
			m_GameInput.Reset();
		}
	}

	InputBase::~InputBase() noexcept
	{
		if (m_CallbackToken)
		{
			if (m_GameInput)
			{
				m_GameInput->UnregisterCallback(m_CallbackToken, UINT64_MAX);
			}
			m_CallbackToken = 0;
		}
	}

	bool InputBase::IsAvailable() const noexcept
	{
		return m_GameInput && m_CallbackToken != 0;
	}

	bool InputBase::IsConnected() const noexcept
	{
		return m_Connected > 0;
	}

	void InputBase::OnGameInputDevice(
		GameInputCallbackToken callbackToken,
		void* context,
		IGameInputDevice* gameInputDevice,
		uint64_t timestamp,
		GameInputDeviceStatus currentStatus,
		GameInputDeviceStatus previousStatus) noexcept
	{
		auto inputDevice = reinterpret_cast<InputBase*>(context);

		const bool wasConnected = (previousStatus & GameInputDeviceConnected) != 0;
		const bool isConnected = (currentStatus & GameInputDeviceConnected) != 0;

		if (isConnected && !wasConnected)
		{
			++inputDevice->m_Connected;
		}
		else if (!isConnected && wasConnected && inputDevice->m_Connected > 0)
		{
			--inputDevice->m_Connected;
		}
	}
}
