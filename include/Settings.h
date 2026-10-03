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

// SKSE/Plugins/InkAndQuill.ini: every setting an integer, clamped, read at kDataLoaded and changed by the MCM
// (written back at once).  A missing file or key is its default.  See docs/SETTINGS.md.
namespace InkAndQuill::Settings {

    struct Setting {
        const char* section;
        const char* key;
        int defaultValue;
        int min;
        int max;
    };

    // A DirectX scan code, not a key that types: while writing it can't also type.
    inline constexpr Setting kEditKey{ "Keys", "Edit", 61, 1, 255 };      // F3: start writing; save and read again
    inline constexpr Setting kInkwellUses{ "Writing", "InkwellUses", 10, 1, 100 };  // saves a full inkwell lasts
    inline constexpr Setting kBloodCost{ "Writing", "BloodCost", 10, 1, 100 };      // % of maximum health per save
    inline constexpr Setting kBlood{ "Writing", "Blood", 1, 0, 1 };                 // offered when there's no ink
    inline constexpr Setting kRequireQuillAndInk{ "Writing", "RequireQuillAndInk", 1, 0, 1 };  // 0: writing is free
    inline constexpr Setting kLayoutTest{ "Debug", "LayoutTest", 0, 0, 1 };  // development only (LayoutTest.h)

    inline constexpr const Setting* kAll[] = { &kEditKey, &kInkwellUses, &kBloodCost, &kBlood, &kRequireQuillAndInk, &kLayoutTest };

    void Load();

    // "Section.Key" (any case: a Papyrus string may come back in another), or nullptr.
    const Setting* Find(std::string_view name);

    int Get(const Setting& setting);

    // Clamped, kept and written to the INI.
    void Set(const Setting& setting, int value);

    inline std::uint32_t EditKey() { return static_cast<std::uint32_t>(Get(kEditKey)); }
    inline bool LayoutTest() { return Get(kLayoutTest) != 0; }

}
