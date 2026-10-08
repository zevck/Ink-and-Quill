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

#include "WritingSound.h"

#include "Settings.h"

namespace InkAndQuill::WritingSound {

    namespace {

        constexpr RE::FormID kDescriptorID = 0x803;  // InkAndQuillWritingSD
        constexpr auto kPlugin = "InkAndQuill.esp";
        constexpr std::uint16_t kCutMs = 30;  // the last scratch's fade when the next begins
        constexpr auto kMinGap = std::chrono::milliseconds(150);  // keys faster than this get no scratch of their own

        RE::BSSoundHandle g_last;  // UI thread only (Editor's Type)
        std::optional<std::chrono::steady_clock::time_point> g_lastStart;

        RE::BGSSoundDescriptorForm* Descriptor()
        {
            static auto* descriptor = []() {
                auto* data = RE::TESDataHandler::GetSingleton();
                auto* form = data ? data->LookupForm<RE::BGSSoundDescriptorForm>(kDescriptorID, kPlugin) : nullptr;
                if (!form) SKSE::log::warn("[Sound] No writing sound: {:X} not found in {}", kDescriptorID, kPlugin);
                return form;
            }();
            return descriptor;
        }

    }

    void Play()
    {
        if (!Settings::WritingSound()) return;
        auto* descriptor = Descriptor();
        auto* audio = RE::BSAudioManager::GetSingleton();
        if (!descriptor || !audio) return;
        const auto now = std::chrono::steady_clock::now();
        if (g_lastStart && now - *g_lastStart < kMinGap) return;
        if (g_last.IsValid() && g_last.IsPlaying()) g_last.FadeOutAndRelease(kCutMs);
        RE::BSSoundHandle handle;
        if (!audio->GetSoundHandle(handle, descriptor) || !handle.Play()) return;
        g_last = handle;
        g_lastStart = now;
    }

}
