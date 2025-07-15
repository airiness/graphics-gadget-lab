#include "Precompiled.h"
#include "Keyboard.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	Keyboard::Keyboard() noexcept : 
		InputBase(GameInputKind::GameInputKindKeyboard)
	{
	}

	void Keyboard::Update() noexcept
	{
		if (m_GameInput && IsConnected())
		{
			auto state = GetState();
			m_StateTracker.Update(state);
		}
	}

	bool Keyboard::IsKeyPressed(KeyCode key) const noexcept
	{
		return m_StateTracker.m_Pressed.IsKeyDown(key);
	}

	bool Keyboard::IsKeyReleased(KeyCode key) const noexcept
	{
		return m_StateTracker.m_Released.IsKeyDown(key);
	}

	bool Keyboard::IsKeyHeld(KeyCode key) const noexcept
	{
		return m_StateTracker.m_LastState.IsKeyDown(key);
	}

	Keyboard::State Keyboard::GetState() noexcept
	{
		State state = {};

		ComPtr<IGameInputReading> reading;
		if (SUCCEEDED(m_GameInput->GetCurrentReading(GameInputKindKeyboard, nullptr, &reading)))
		{
			auto readCount = reading->GetKeyState(MaxKeyStateCount, m_KeyStates);
			for (uint32_t index = 0; index < readCount; ++index)
			{
				auto vk = static_cast<int32_t>(m_KeyStates[index].virtualKey);
				state.SetKeyBit(vk);
			}
		}

		return state;
	}

	// StateTracker
	void Keyboard::StateTracker::Update(const State& state) noexcept
	{
		auto currPtr = reinterpret_cast<const uint32_t*>(&state);
		auto prevPtr = reinterpret_cast<const uint32_t*>(&m_LastState);
		auto releasedPtr = reinterpret_cast<uint32_t*>(&m_Released);
		auto pressedPtr = reinterpret_cast<uint32_t*>(&m_Pressed);

		for (size_t index = 0; index < (256 / 32); ++index)
		{
			*pressedPtr = *currPtr & ~(*prevPtr);
			*releasedPtr = ~(*currPtr) & *prevPtr;

			++currPtr;
			++prevPtr;
			++releasedPtr;
			++pressedPtr;
		}

		m_LastState = state;
	}

	void Keyboard::StateTracker::Reset() noexcept
	{
		std::memset(this, 0, sizeof(StateTracker));
	}
}