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

// The quill at the caret: the game's own quill model, in the book menu's 3D scene.
// See docs/EDITOR.md#quill-cursor.
namespace InkAndQuill::QuillCursor {

    // Edit mode on: puts the quill on the open book.  Game thread.
    void Show();

    // Edit mode off, or the book closing: takes it off again.
    void Hide();

    // Every frame of the book menu: the quill follows the book (it moves as it opens).
    void Follow();

    // Text was typed (not the caret moved, not text erased): the quill wiggles a moment, as if writing it.
    void Wrote();

    // Development (Settings::kQuillAdjust): a numpad key moves, turns or scales the quill, or logs
    // its pose with the caret's point.  True when the key was the quill's.  See docs/EDITOR.md#quill-cursor.
    bool Adjust(std::uint32_t scanCode);

}
