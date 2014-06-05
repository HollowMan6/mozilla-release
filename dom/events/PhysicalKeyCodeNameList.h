/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This header file defines all DOM code name which are used for DOM
 * KeyboardEvent.code.
 * You must define NS_DEFINE_PHYSICAL_KEY_CODE_NAME macro before including this.
 *
 * It must have two arguments, (aCPPName, aDOMCodeName)
 * aCPPName is usable name for a part of C++ constants.
 * aDOMCodeName is the actual value.
 */

#define NS_DEFINE_PHYSICAL_KEY_CODE_NAME_INTERNAL(aCPPName, aDOMCodeName) \
  NS_DEFINE_PHYSICAL_KEY_CODE_NAME(aCPPName, aDOMCodeName)

#define DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(aName) \
  NS_DEFINE_PHYSICAL_KEY_CODE_NAME_INTERNAL(aName, #aName)

// Unknown key
NS_DEFINE_PHYSICAL_KEY_CODE_NAME_INTERNAL(UNKNOWN, "")

// Writing system keys
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Backquote)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Backslash)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Backspace)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(BracketLeft)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(BracketRight)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Comma)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Digit0)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Digit1)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Digit2)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Digit3)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Digit4)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Digit5)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Digit6)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Digit7)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Digit8)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Digit9)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Equal)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(IntlBackslash)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(IntlHash)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(IntlRo)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(IntlYen)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyA)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyB)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyC)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyD)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyE)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyF)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyG)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyH)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyI)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyJ)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyK)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyL)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyM)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyN)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyO)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyP)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyQ)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyR)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyS)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyT)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyU)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyV)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyW)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyX)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyY)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KeyZ)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Minus)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Period)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Quote)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Semicolon)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Slash)

// Functional keys
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(AltLeft)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(AltRight)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(CapsLock)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(ContextMenu)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(ControlLeft)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(ControlRight)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Enter)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(OSLeft)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(OSRight)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(ShiftLeft)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(ShiftRight)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Space)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Tab)

// IME keys
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Convert)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(KanaMode)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Lang1)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Lang2)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Lang3)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Lang4)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Lang5)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NonConvert)

// Control pad section
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Delete)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(End)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Help)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Home)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Insert)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(PageDown)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(PageUp)

// Arrow pad section
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(ArrowDown)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(ArrowLeft)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(ArrowRight)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(ArrowUp)

// Numpad section
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumLock)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Numpad0)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Numpad1)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Numpad2)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Numpad3)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Numpad4)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Numpad5)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Numpad6)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Numpad7)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Numpad8)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Numpad9)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadAdd)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadBackspace)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadClear)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadClearEntry)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadComma)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadDecimal)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadDivide)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadEnter)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadEqual)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadMemoryAdd)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadMemoryClear)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadMemoryRecall)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadMemoryStore)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadMemorySubtract)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadMultiply)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadParenLeft)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadParenRight)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(NumpadSubtract)

// Function section
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Escape)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F1)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F2)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F3)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F4)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F5)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F6)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F7)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F8)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F9)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F10)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F11)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F12)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F13)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F14)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F15)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F16)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F17)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F18)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F19)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F20)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F21)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F22)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F23)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(F24)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Fn)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(FLock)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(PrintScreen)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(ScrollLock)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Pause)

// Media keys
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(BrowserBack)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(BrowserFavorites)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(BrowserForward)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(BrowserHome)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(BrowserRefresh)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(BrowserSearch)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(BrowserStop)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Eject)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(LaunchApp1)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(LaunchApp2)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(LaunchMail)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(MediaPlayPause)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(MediaSelect)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(MediaStop)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(MediaTrackNext)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(MediaTrackPrevious)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Power)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Sleep)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(VolumeDown)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(VolumeMute)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(VolumeUp)
DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(WakeUp)

// Legacy keys
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Abort)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Hyper)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Resume)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Super)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Suspend)
// DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME(Turbo)

#undef DEFINE_PHYSICAL_KEY_CODE_NAME_WITH_SAME_NAME
#undef NS_DEFINE_PHYSICAL_KEY_CODE_NAME_INTERNAL
