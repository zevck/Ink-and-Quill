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

// The paper the book menu draws the text on: the book model's "Base Page1" to "Base Page4", skinned sheets that fold
// and turn.  See docs/EDITOR.md#quill-cursor.
namespace InkAndQuill::QuillPaper {

    // The open book's sheets, read once per book (their vertices, UVs, skin weights and triangles).  Game thread.
    void Read(RE::NiAVObject* book);

    void Clear();

    // The point of page slot `slot`'s sheet that shows texture point (u, v), in the world, as the sheet is bent now.
    std::optional<RE::NiPoint3> At(int slot, float u, float v);

    // A vertex buffer's 16-bit float; its stride (the descriptor's low nibble, in 4 bytes); a vertex's position.
    float HalfToFloat(std::uint16_t h);
    std::uint32_t Stride(const RE::BSGraphics::VertexDesc& desc);
    RE::NiPoint3 Position(const std::uint8_t* vertex, bool full);

}
