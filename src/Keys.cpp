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

#include "Keys.h"

#include <Windows.h>

namespace InkAndQuill::Keys {

    bool IsModifier(std::uint32_t code) { return std::ranges::find(kModifiers, code) != std::end(kModifiers); }

    Problem Check(std::uint32_t code)
    {
        if (code < 1 || code > 255) return Problem::NotKeyboard;
        static constexpr std::uint32_t kEditing[] = { kEscape, kBackspace, kEnter, kKeypadEnter, kDelete,
                                                      kLeft, kRight, kUp, kDown, kHome, kEnd };
        if (IsModifier(code) || std::ranges::find(kEditing, code) != std::end(kEditing)) return Problem::Types;
        // Anything that gives a character with the player's layout (0x80 and up: an extended key, E0-prefixed).
        const UINT scan = (code & 0x80) ? (0xE000 | (code & 0x7F)) : code;
        const UINT vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
        BYTE state[256] = {};
        WCHAR chars[4] = {};
        // Flag 4: leave the keyboard state alone (no dead key left pending).
        return vk != 0 && ToUnicode(vk, code & 0x7F, state, chars, 4, 4) != 0 ? Problem::Types : Problem::None;
    }

}
