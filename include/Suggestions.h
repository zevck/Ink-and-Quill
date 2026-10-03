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

// Inline completion: a client's candidates for the text at the caret, shown one at a time, faded, after it
// (book.swf draws them apart from the text).  See docs/EDITOR.md#suggestions.
namespace InkAndQuill::Suggestions {

    // Shows completions at the caret (cleaned: one line, no markers); none clears them.  UI thread, while writing.
    void Show(const std::vector<std::string>& completions);

    void Clear();

    // A key while writing: true if it was the suggestion's (Tab, Shift+Tab, Right, Escape).  Right fills
    // accepted with the text to type.  Any other key clears the suggestion and returns false.
    bool HandleKey(std::uint32_t scanCode, std::string& accepted);

}
