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

#include "Suggestions.h"

#include "BookMovie.h"
#include "Keys.h"
#include "Strings.h"

#include <Windows.h>

namespace InkAndQuill::Suggestions {

    namespace {

        using Book::Call;

        // One line of plain text: no control characters (U+001F separates the list), no lock or blood markers.
        std::string Clean(const std::string& text)
        {
            std::wstring wide = Strings::Wide(text);
            std::erase_if(wide, [](wchar_t c) { return c < 32 || (c >= 0xE000 && c <= 0xF8FF); });
            return Strings::Utf8(wide);
        }

    }

    void Show(const std::vector<std::string>& completions)
    {
        std::string list;
        for (const auto& completion : completions) {
            const auto clean = Clean(completion);
            if (clean.empty()) continue;
            if (!list.empty()) list += '\x1F';
            list += clean;
        }
        if (list.empty()) {
            Clear();
        } else {
            Call("EditSuggest", nullptr, list.c_str());
        }
    }

    void Clear() { Call("EditSuggestClear"); }

    bool HandleKey(std::uint32_t scanCode, std::string& accepted)
    {
        RE::GFxValue showing;
        if (!Call("EditSuggesting", &showing) || !showing.IsBool() || !showing.GetBool()) return false;
        if (scanCode == Keys::kTab) {
            Call("EditSuggestNext", nullptr, (GetKeyState(VK_SHIFT) & 0x8000) ? "-1" : "1");
            return true;
        }
        if (scanCode == Keys::kRight) {
            RE::GFxValue text;
            if (Call("EditSuggestTake", &text) && text.IsString()) accepted = text.GetString();
            return true;
        }
        Clear();
        return scanCode == Keys::kEscape;
    }

}
