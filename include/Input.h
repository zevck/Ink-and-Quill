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

// The keyboard while a book is open: the editor's keys read, then taken from the game's input so other mods'
// hotkeys don't fire; clients' own keys let through.  See docs/EDITOR.md#input.
namespace InkAndQuill::Input {

    // Once, at kDataLoaded: the input sink, first in line.
    void Register();

    // At the first kPostLoadGame or kNewGame (after Wheeler hooks the same call at kDataLoaded): the input dispatch hook.
    void InstallHook();

    // While writing, every keyboard event is taken from the game but clients' own keys (a new entry): this client's
    // set, replacing its last.  Keys that type or edit, and the edit key, are refused.  Returns how many were kept.
    int SetClientKeys(const void* client, const std::vector<std::uint32_t>& codes);

}
