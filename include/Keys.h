/*
 * Ink & Quill - a Skyrim SKSE writing framework: the player writes in books in the
 * book menu, with quill, ink or blood, for any mod that gives the text a meaning.
 * Copyright (C) 2026 Zevick
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

// The keys the editor uses (DirectX scan codes), and which keys may be the edit key or a client's key.
namespace InkAndQuill::Keys {

    inline constexpr std::uint32_t kEscape = 0x01, kBackspace = 0x0E, kEnter = 0x1C, kKeypadEnter = 0x9C, kDelete = 0xD3;
    inline constexpr std::uint32_t kLeft = 0xCB, kRight = 0xCD, kUp = 0xC8, kDown = 0xD0, kHome = 0xC7, kEnd = 0xCF;
    // Modifiers only change other keys: Shift, Ctrl, Alt (left and right), Caps Lock.
    inline constexpr std::uint32_t kModifiers[] = { 0x2A, 0x36, 0x1D, 0x9D, 0x38, 0xB8, 0x3A };

    bool IsModifier(std::uint32_t code);

    // Input thread: this press, or a held key's repeat (like a text box), should produce input now.
    bool ShouldRepeat(const RE::ButtonEvent* button, std::uint32_t code);

    enum class Problem { None, NotKeyboard, Types };

    // Whether a key can be the edit key or a client's key: not a keyboard key (SkyUI also offers mouse and gamepad
    // buttons), or one that types or edits while writing (the editor's keys, and anything giving a character).
    Problem Check(std::uint32_t code);

}
