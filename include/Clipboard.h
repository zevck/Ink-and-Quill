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

// The Windows clipboard for the editor's paste and copy (docs/EDITOR.md#input).  The game window's thread only.
namespace InkAndQuill::Clipboard {

    // The clipboard's text made safe to type (UTF-8): line breaks as "\r", tabs as spaces, no other control
    // characters and no private-use characters (Ink & Quill's blood and lock markers among them).  Empty: none.
    std::string ReadForTyping();

    // Puts text (UTF-8, "\n" line breaks; private-use markers dropped) on the clipboard.  False: it couldn't.
    bool Write(std::string_view text);

}
