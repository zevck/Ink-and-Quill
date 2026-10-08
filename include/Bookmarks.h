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

// Bookmarks a client puts in a book's reading text (<a href="bookmark:Name">), and the list of them drawn on the
// book while reading.  See docs/EDITOR.md#bookmarks.
namespace InkAndQuill::Bookmarks {

    // Input thread; the work is queued.  The list key: opens the list over the open spread (a notice if the book has
    // none), or closes it.
    void OnListKey();

    // While the list is open every key but Left and Right is its (those turn it, as the game's page turn): up/down
    // choose, Enter goes there, Esc or the list key close it.
    bool IsListOpen();
    void OnKeyInList(std::uint32_t code);

    void OnBookClosed();

    // UI thread: writing begins, so the list closes (it never stays up over the editor).
    void CloseList();

}
