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

// The Quill of Gemination and Special Ink: writing in Special Ink, Enthir's stock, the quill on Maven, and Snazzy's
// versions when the FOMOD chose them.  See docs/EDITOR.md#special-ink.
namespace InkAndQuill::Gemination {

    // kDataLoaded: the FOMOD's choice, the items; with writing on, Enthir stocks Ink & Quill's Special Ink.
    void OnDataLoaded(bool writingOn);

    // A load or new game: the quill (and Snazzy's recipe) on Maven, once per save, if Vald's Debt is past the hand-over.
    void OnGameLoaded();

    // The co-save (Serialization): whether this save's Maven has had the quill.
    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t type);
    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t version);
    void Revert();

    // Writing.SpecialInkPrice: Ink & Quill's Special Ink's base value (Snazzy's is left alone).
    void ApplyPrice();

    // Enthir sells Special Ink: no Snazzy Special Ink chosen in the FOMOD.
    bool SoldByEnthir();

    // The player carries the Quill of Gemination, and Special Ink.
    bool HasQuill();
    bool HasInk();

    // Game thread: one bottle of Special Ink from the player.  Its FormID, 0 if there was none.
    RE::FormID UseInk();

    // The Quill of Gemination's record (its model is the quill cursor's while the player carries it).
    RE::TESBoundObject* Quill();

}
