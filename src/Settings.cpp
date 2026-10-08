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

        // Values by kAll index: read on the main thread, set from the MCM's natives.
        std::array<std::atomic<int>, std::size(kAll)> g_values;

        std::size_t IndexOf(const Setting& setting)
        {
            for (std::size_t i = 0; i < std::size(kAll); ++i) {
                if (kAll[i] == &setting) return i;
            }
            return 0;
        }

        std::string Path() { return std::filesystem::absolute(kPath).string(); }
    }

    std::string IniPath() { return Path(); }

    namespace {
        // Debug.Logging: the log's level, at once.
        void ApplyLogLevel()
        {
            if (auto log = spdlog::default_logger()) log->set_level(Get(kDebugLog) ? spdlog::level::debug : spdlog::level::info);
        }
    }

    void Load()
    {
        const auto path = Path();
        for (std::size_t i = 0; i < std::size(kAll); ++i) {
            const auto& s = *kAll[i];
            g_values[i] = std::clamp(static_cast<int>(GetPrivateProfileIntA(s.section, s.key, s.defaultValue, path.c_str())), s.min, s.max);
        }
        SKSE::log::info("[Settings] Edit key 0x{:X}, inkwell uses {}, blood {} ({}%), quill and ink {}",
                        EditKey(), Get(kInkwellUses), Get(kBlood) ? "on" : "off", Get(kBloodCost),
                        Get(kRequireQuillAndInk) ? "required" : "not required");
        ApplyLogLevel();
    }

    const Setting* Find(std::string_view name)
    {
        for (const auto* s : kAll) {
            if (_stricmp(std::string(name).c_str(), std::format("{}.{}", s->section, s->key).c_str()) == 0) return s;
        }
        return nullptr;
    }

    int Get(const Setting& setting) { return g_values[IndexOf(setting)]; }

    void Set(const Setting& setting, int value)
    {
        value = std::clamp(value, setting.min, setting.max);
        g_values[IndexOf(setting)] = value;
        if (!WritePrivateProfileStringA(setting.section, setting.key, std::to_string(value).c_str(), Path().c_str())) {
            SKSE::log::error("[Settings] Couldn't write {}.{} to {}", setting.section, setting.key, kPath);
        }
        SKSE::log::info("[Settings] {}.{} = {}", setting.section, setting.key, value);
        if (&setting == &kDebugLog) ApplyLogLevel();
    }

}
