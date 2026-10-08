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

#include "Bookmarks.h"

#include "BookMovie.h"
#include "Editor.h"
#include "Keys.h"
#include "Settings.h"
#include "Strings.h"
#include "Tasks.h"

namespace InkAndQuill::Bookmarks {

    namespace {

        std::atomic<bool> g_listOpen = false;  // set on the UI thread, read by the input filter

        // Queued work for the open book, read and not written in (checked again when it runs: either may have changed).
        void Queue(std::function<void()> work)
        {
            Tasks::QueueUI([work = std::move(work)]() {
                if (Editor::IsBookOpen() && !Editor::IsWriting()) work();
            }, "Bookmarks");
        }

        // The list's answer to a key: anything but "moved" closed it.
        void ListKey(const char* key)
        {
            Queue([key]() {
                RE::GFxValue result;
                if (!Book::Call("ContentsKey", &result, key)) {
                    g_listOpen = false;
                    return;
                }
                const std::string what = result.IsString() ? result.GetString() : "";
                if (what != "moved") {
                    g_listOpen = false;
                    SKSE::log::info("[Bookmarks] List {}", what == "jumped" ? "used" : "closed");
                }
            });
        }

    }

    bool IsListOpen() { return g_listOpen; }

    void OnListKey()
    {
        if (g_listOpen) {
            ListKey("close");
            return;
        }
        Queue([]() {
            RE::GFxValue opened;
            const bool ok = Book::Call("ContentsOpen", &opened, Strings::Get("$IQ_Contents").c_str()) && opened.IsBool() && opened.GetBool();
            g_listOpen = ok;
            if (!ok) Tasks::Notify(Strings::Get("$IQ_NoBookmarks"));
        });
    }

    void OnKeyInList(std::uint32_t code)
    {
        using namespace Keys;
        if (code == kUp) ListKey("up");
        else if (code == kDown) ListKey("down");
        else if (code == kEnter || code == kKeypadEnter) ListKey("enter");
        else if (code == kEscape || code == Settings::ContentsKey()) ListKey("close");
    }

    void OnBookClosed() { g_listOpen = false; }

    void CloseList()
    {
        if (!g_listOpen.exchange(false)) return;
        RE::GFxValue result;
        Book::Call("ContentsKey", &result, "close");
    }

}
