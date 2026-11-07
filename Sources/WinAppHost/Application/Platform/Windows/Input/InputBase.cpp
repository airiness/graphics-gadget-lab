#include "Application/Platform/Windows/Input/InputBase.h"
#include "AppRuntimeLog.h"
#include "GGLabFoundation/Platform/Win/HResult.h"

#include <algorithm>
#include <mutex>
#include <utility>

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
		const std::scoped_lock lock(m_DeviceMutex);
		return !m_ConnectedDevices.empty();
	}

	void InputBase::CopyConnectedDevices(
		std::vector<ComPtr<IGameInputDevice>>& devices) const noexcept
	{
		// Release the previous snapshot outside the callback's device-list lock.
		devices.clear();
		const std::scoped_lock lock(m_DeviceMutex);
		devices = m_ConnectedDevices;
	}

	void InputBase::OnGameInputDevice(
		GameInputCallbackToken,
		void* context,
		IGameInputDevice* gameInputDevice,
		uint64_t,
		GameInputDeviceStatus currentStatus,
		GameInputDeviceStatus) noexcept
	{
		auto inputDevice = reinterpret_cast<InputBase*>(context);
		const bool isConnected = (currentStatus & GameInputDeviceConnected) != 0;
		// Declared before the guard so a removed device is released after unlocking.
		ComPtr<IGameInputDevice> disconnectedDevice;
		const std::scoped_lock lock(inputDevice->m_DeviceMutex);
		auto& devices = inputDevice->m_ConnectedDevices;
		const auto found = std::ranges::find_if(devices,
			[gameInputDevice](const ComPtr<IGameInputDevice>& device)
			{
				return device.Get() == gameInputDevice;
			});
		if (isConnected && found == devices.end())
		{
			devices.emplace_back(gameInputDevice);
		}
		else if (!isConnected && found != devices.end())
		{
			disconnectedDevice = std::move(*found);
			devices.erase(found);
		}
	}
}
