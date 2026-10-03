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

#include "Clipboard.h"

#include "Strings.h"

#include <Windows.h>

namespace InkAndQuill::Clipboard {

    namespace {
        // A paste longer than this is cut: the editor's field is one tall text field.
        constexpr std::size_t kMaxPaste = 20000;

        bool IsPrivateUse(wchar_t c) { return c >= 0xE000 && c <= 0xF8FF; }

        // The clipboard, open while this lives; the game's window owns what's put on it (SetClipboardData fails
        // without an owner).  Another program holding it open is common: logged, and nothing happens.
        class Open {
        public:
            Open()
            {
                HWND window = GetActiveWindow();
                open_ = OpenClipboard(window ? window : GetForegroundWindow()) != 0;
                if (!open_) SKSE::log::warn("[Clipboard] Couldn't open the clipboard (error {})", GetLastError());
            }
            ~Open()
            {
                if (open_) CloseClipboard();
            }
            Open(const Open&) = delete;
            Open& operator=(const Open&) = delete;
            explicit operator bool() const { return open_; }

        private:
            bool open_ = false;
        };
    }

    std::string ReadForTyping()
    {
        std::wstring text;
        {
            const Open clipboard;
            if (!clipboard) return {};
            HANDLE data = GetClipboardData(CF_UNICODETEXT);
            const auto* chars = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
            if (!chars) return {};
            try {
                text = chars;
            } catch (...) {
                GlobalUnlock(data);
                throw;
            }
            GlobalUnlock(data);
        }
        std::wstring clean;
        for (std::size_t i = 0; i < text.size() && clean.size() < kMaxPaste; ++i) {
            const wchar_t c = text[i];
            if (c == L'\r' || c == L'\n') {
                if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
                clean += L'\r';
            } else if (c == L'\t') {
                clean += L' ';
            } else if (c >= 32 && !IsPrivateUse(c)) {
                clean += c;
            }
        }
        return Strings::Utf8(clean);
    }

    bool Write(std::string_view text)
    {
        std::wstring out;
        for (const wchar_t c : Strings::Wide(text)) {
            if (IsPrivateUse(c)) continue;
            if (c == L'\n') out += L'\r';
            out += c;
        }
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (out.size() + 1) * sizeof(wchar_t));
        auto* chars = memory ? static_cast<wchar_t*>(GlobalLock(memory)) : nullptr;
        if (!chars) {
            if (memory) GlobalFree(memory);
            SKSE::log::warn("[Clipboard] No memory for the copy");
            return false;
        }
        std::memcpy(chars, out.c_str(), (out.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(memory);
        const Open clipboard;
        if (!clipboard) {
            GlobalFree(memory);
            return false;
        }
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, memory)) {
            SKSE::log::warn("[Clipboard] The clipboard didn't take the copy (error {})", GetLastError());
            GlobalFree(memory);
            return false;
        }
        return true;
    }

}
