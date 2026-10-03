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

#include "Settings.h"

#include <Windows.h>

namespace InkAndQuill::Settings {

    namespace {
        constexpr auto kPath = "Data/SKSE/Plugins/InkAndQuill.ini";

        std::uint32_t g_editKey = 61;
        std::uint32_t g_removeKey = 68;
        bool g_layoutTest = false;

        std::uint32_t ReadKey(const char* key, std::uint32_t fallback)
        {
            const auto path = std::filesystem::absolute(kPath).string();
            const auto value = GetPrivateProfileIntA("Keys", key, static_cast<int>(fallback), path.c_str());
            return value >= 1 && value <= 255 ? static_cast<std::uint32_t>(value) : fallback;
        }
    }

    void Load()
    {
        g_editKey = ReadKey("Edit", 61);
        g_removeKey = ReadKey("Remove", 68);
        const auto path = std::filesystem::absolute(kPath).string();
        g_layoutTest = GetPrivateProfileIntA("Debug", "LayoutTest", 0, path.c_str()) == 1;
        SKSE::log::info("[Settings] Edit key 0x{:X}, remove key 0x{:X}", g_editKey, g_removeKey);
    }

    std::uint32_t EditKey() { return g_editKey; }

    std::uint32_t RemoveKey() { return g_removeKey; }

    bool LayoutTest() { return g_layoutTest; }

}
