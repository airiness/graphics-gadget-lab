#include "ApplicationInput.h"

#include <utility>

namespace gglab
{
	namespace
	{
		template<typename Enum, size_t Count>
		[[nodiscard]] constexpr size_t ToIndex(Enum value) noexcept
		{
			const size_t index = static_cast<size_t>(value);
			return index < Count ? index : Count;
		}
	}

	void ApplicationInput::Publish(const ApplicationInputSnapshot& snapshot) noexcept
	{
		m_Previous = std::exchange(m_Current, snapshot);
	}

	void ApplicationInput::Reset() noexcept
	{
		m_Previous = {};
		m_Current = {};
		m_KeyboardCapturedByUI = false;
		m_PointerCapturedByUI = false;
	}

	bool ApplicationInput::IsKeyPressed(AppInputKey key) const noexcept
	{
		const size_t index = ToIndex<AppInputKey, AppInputKeyCount>(key);
		return index < AppInputKeyCount && m_Current.m_KeysHeld[index] &&
			!m_Previous.m_KeysHeld[index];
	}

	bool ApplicationInput::IsKeyReleased(AppInputKey key) const noexcept
	{
		const size_t index = ToIndex<AppInputKey, AppInputKeyCount>(key);
		return index < AppInputKeyCount && !m_Current.m_KeysHeld[index] &&
			m_Previous.m_KeysHeld[index];
	}

	bool ApplicationInput::IsKeyHeld(AppInputKey key) const noexcept
	{
		const size_t index = ToIndex<AppInputKey, AppInputKeyCount>(key);
		return index < AppInputKeyCount && m_Current.m_KeysHeld[index];
	}

	bool ApplicationInput::IsPointerButtonPressed(AppPointerButton button) const noexcept
	{
		const size_t index = ToIndex<AppPointerButton, AppPointerButtonCount>(button);
		return index < AppPointerButtonCount && m_Current.m_PointerButtonsHeld[index] &&
			!m_Previous.m_PointerButtonsHeld[index];
	}

	bool ApplicationInput::IsPointerButtonReleased(AppPointerButton button) const noexcept
	{
		const size_t index = ToIndex<AppPointerButton, AppPointerButtonCount>(button);
		return index < AppPointerButtonCount && !m_Current.m_PointerButtonsHeld[index] &&
			m_Previous.m_PointerButtonsHeld[index];
	}

	bool ApplicationInput::IsPointerButtonHeld(AppPointerButton button) const noexcept
	{
		const size_t index = ToIndex<AppPointerButton, AppPointerButtonCount>(button);
		return index < AppPointerButtonCount && m_Current.m_PointerButtonsHeld[index];
	}

	AppInputVector2 ApplicationInput::GetPointerPosition() const noexcept
	{
		return m_Current.m_PointerPosition;
	}

	AppInputVector2 ApplicationInput::GetPointerDelta() const noexcept
	{
		return m_Current.m_PointerDelta;
	}

	int64_t ApplicationInput::GetScrollDeltaY() const noexcept
	{
		return m_Current.m_ScrollDeltaY;
	}

	void ApplicationInput::SetPointerMode(AppPointerMode mode) noexcept
	{
		if (m_PointerMode == mode)
		{
			return;
		}

		m_PointerMode = mode;
		m_Previous.m_PointerButtonsHeld = m_Current.m_PointerButtonsHeld;
		m_Current.m_PointerDelta = {};
		m_Current.m_ScrollDeltaY = 0;
	}

	AppPointerMode ApplicationInput::GetPointerMode() const noexcept
	{
		return m_PointerMode;
	}

	void ApplicationInput::SetUICaptureState(
		bool keyboardCaptured, bool pointerCaptured) noexcept
	{
		m_KeyboardCapturedByUI = keyboardCaptured;
		m_PointerCapturedByUI = pointerCaptured;
	}

	bool ApplicationInput::IsKeyboardCapturedByUI() const noexcept
	{
		return m_KeyboardCapturedByUI;
	}

	bool ApplicationInput::IsPointerCapturedByUI() const noexcept
	{
		return m_PointerCapturedByUI;
	}

	bool ApplicationInput::IsAvailable() const noexcept
	{
		return m_Current.m_IsAvailable;
	}
}
