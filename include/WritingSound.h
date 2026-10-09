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

// The quill's sounds on the paper while writing (their volume: Settings > Audio, Writing).  See
// docs/EDITOR.md#the-writing-sound.
namespace InkAndQuill::WritingSound {

    // Text was typed (not only spaces and line breaks): a scratch (none within 150 ms of the last sound), or a lone
    // punctuation mark's own taps and strokes, which always sound.  UI thread.
    void Play(std::string_view text);

    // Every frame of the book menu: plays a mark's later sounds as they come due.  Clear: the book closed, drop them.
    void Tick();
    void Clear();

}
