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
    inline constexpr Setting kContentsKey{ "Keys", "Contents", 0xC7, 1, 255 };  // Home, reading: the bookmark list
    inline constexpr Setting kInkwellUses{ "Writing", "InkwellUses", 10, 0, 100 };  // saves a full inkwell lasts; 0: never runs dry
    inline constexpr Setting kBloodCost{ "Writing", "BloodCost", 10, 1, 100 };      // % of maximum health per save
    inline constexpr Setting kBlood{ "Writing", "Blood", 1, 0, 1 };                 // offered when there's no ink
    inline constexpr Setting kRequireQuillAndInk{ "Writing", "RequireQuillAndInk", 1, 0, 1 };  // 0: writing is free
    inline constexpr Setting kQuillCursor{ "Writing", "QuillCursor", 1, 0, 1 };  // the game's quill at the caret, in place of it
    inline constexpr Setting kWritingSound{ "Writing", "Sound", 1, 0, 1 };      // a quill scratch for each typed key
    inline constexpr Setting kQuillAdjust{ "Debug", "QuillAdjust", 0, 0, 1 };    // the numpad poses the quill (QuillCursor.h)
    inline constexpr Setting kDebugLog{ "Debug", "Logging", 0, 0, 1 };           // the log at debug level, not info

    inline constexpr const Setting* kAll[] = { &kEditKey, &kContentsKey, &kInkwellUses, &kBloodCost, &kBlood, &kRequireQuillAndInk, &kQuillCursor, &kWritingSound, &kQuillAdjust,
                                               &kDebugLog };

    void Load();

    // SKSE/Plugins/InkAndQuill.ini, absolute.  Other mods' own INI files go in kModFilesFolder ([Materials]).
    std::string IniPath();
    inline constexpr auto kModFilesFolder = "Data/SKSE/Plugins/InkAndQuill";

    // "Section.Key" (any case: a Papyrus string may come back in another), or nullptr.
    const Setting* Find(std::string_view name);

    int Get(const Setting& setting);

    // Clamped, kept and written to the INI.
    void Set(const Setting& setting, int value);

    inline std::uint32_t EditKey() { return static_cast<std::uint32_t>(Get(kEditKey)); }
    inline std::uint32_t ContentsKey() { return static_cast<std::uint32_t>(Get(kContentsKey)); }
    inline bool QuillCursor() { return Get(kQuillCursor) != 0; }
    inline bool QuillAdjust() { return Get(kQuillAdjust) != 0; }
    inline bool WritingSound() { return Get(kWritingSound) != 0; }

}
