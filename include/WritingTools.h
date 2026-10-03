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

// Quill, ink and blood.  A used inkwell is the same item renamed, "Inkwell (9/10)"; the
// vanilla record is never changed.  See docs/EDITOR.md#quill-ink-and-blood.
namespace InkAndQuill::WritingTools {

    // kDataLoaded: finds the ESP's quill and inkwell lists.
    void OnDataLoaded();

    // The player carries a quill.
    bool HasQuill();

    // The player carries an inkwell with ink.
    bool HasInk();

    enum class Ink { None, Used, RanDry };

    // Game thread: one use of ink from the player's emptiest inkwell, renamed with the uses
    // left.  Ink::RanDry: that was its last use, and it's gone.  Ink::None: no inkwell.
    Ink UseInk();

    // Writing in blood wouldn't leave the player below 1 health.
    bool CanBleed();

    // Game thread: writing in blood costs the player a share of their maximum health; false
    // (nothing taken) if CanBleed is false.
    bool Bleed();

}
