#include "Precompiled.h"
#include "InputBase.h"
#include "Utility.h"

namespace gglab
{
	InputBase::InputBase(GameInputKind inputKind) noexcept
	{
		utility::ThrowIfFailed(GameInputCreate(&m_GameInput));

		utility::ThrowIfFailed(m_GameInput->RegisterDeviceCallback(
			nullptr,
			inputKind,
			GameInputDeviceConnected,
			GameInputBlockingEnumeration,
			this,
			OnGameInputDevice,
			&m_CallbackToken));
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

