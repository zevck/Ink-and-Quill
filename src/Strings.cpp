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

#include "Strings.h"

#include <Windows.h>
#include <fstream>
#include <iterator>

namespace InkAndQuill::Strings {

    namespace {

        struct Hash : std::hash<std::string_view> {
            using is_transparent = void;
        };
        std::unordered_map<std::string, std::string, Hash, std::equal_to<>> g_strings;

        std::string Language()
        {
            auto* ini = RE::INISettingCollection::GetSingleton();
            auto* setting = ini ? ini->GetSetting("sLanguage:General") : nullptr;
            std::string language = setting && setting->GetString() ? setting->GetString() : "";
            std::ranges::transform(language, language.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return language.empty() ? "ENGLISH" : language;
        }

        // A translation file: UTF-16 LE with a BOM, one "$key<tab>text" per line.
        bool Read(const std::string& language)
        {
            const auto path = std::format("Data/Interface/Translations/InkAndQuill_{}.txt", language);
            std::ifstream file(path, std::ios::binary);
            if (!file) return false;
            const std::string bytes{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
            std::wstring text(bytes.size() / 2, L'\0');
            std::memcpy(text.data(), bytes.data(), text.size() * 2);
            if (text.starts_with(L'\xFEFF')) text.erase(0, 1);
            int count = 0;
            for (std::size_t start = 0; start < text.size();) {
                auto end = text.find(L'\n', start);
                if (end == std::wstring::npos) end = text.size();
                std::wstring line = text.substr(start, end - start);
                start = end + 1;
                if (line.ends_with(L'\r')) line.pop_back();
                const auto tab = line.find(L'\t');
                if (!line.starts_with(L'$') || tab == std::wstring::npos) continue;
                auto utf8 = [](std::wstring_view w) {
                    std::string out(WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), static_cast<int>(out.size()), nullptr, nullptr);
                    return out;
                };
                std::string value = utf8(std::wstring_view(line).substr(tab + 1));
                for (std::size_t at = 0; (at = value.find("\\n", at)) != std::string::npos;) value.replace(at, 2, "\n");
                g_strings.insert_or_assign(utf8(std::wstring_view(line).substr(0, tab)), std::move(value));
                ++count;
            }
            SKSE::log::info("[Strings] {} strings from {}", count, path);
            return true;
        }

    }

    void Load()
    {
        g_strings.clear();
        // English first, so a key a translation lacks still has its text.
        const bool english = Read("ENGLISH");
        if (const auto language = Language(); language != "ENGLISH" && !Read(language)) {
            SKSE::log::info("[Strings] No translation for {}: English", language);
        }
        if (!english) SKSE::log::error("[Strings] Interface/Translations/InkAndQuill_ENGLISH.txt is missing");
    }

    const std::string& Get(std::string_view key)
    {
        if (const auto it = g_strings.find(key); it != g_strings.end()) return it->second;
        return g_strings.emplace(std::string(key), std::string(key)).first->second;
    }

}
