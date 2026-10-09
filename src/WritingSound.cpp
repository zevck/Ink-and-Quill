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

#include "Strings.h"

namespace InkAndQuill::WritingSound {

    namespace {

        constexpr RE::FormID kScratchID = 0x803;  // InkAndQuillWritingSD
        constexpr RE::FormID kDotID = 0x804;      // InkAndQuillDotSD
        constexpr auto kPlugin = "InkAndQuill.esp";
        constexpr std::uint16_t kCutMs = 30;   // the last scratch's fade when the next begins
        constexpr std::uint16_t kFlickFadeMs = 15;
        constexpr int kFlickMs = 60;           // a short stroke: a scratch cut off this soon
        constexpr auto kMinGap = std::chrono::milliseconds(150);  // keys faster than this get no scratch of their own

        // A mark's sounds: a tap (the dot), a stroke (a whole scratch) or a flick (a short stroke), each so many ms
        // after the one before.
        enum class Sound { kTap, kStroke, kFlick };
        struct Step
        {
            Sound sound;
            int afterMs = 0;
        };

        const std::unordered_map<wchar_t, std::vector<Step>>& Recipes()
        {
            using enum Sound;
            static const std::vector<Step> kDot{ { kTap } };
            static const std::vector<Step> kComma{ { kFlick } };
            static const std::vector<Step> kColon{ { kTap }, { kTap, 90 } };
            static const std::vector<Step> kSemicolon{ { kTap }, { kFlick, 90 } };
            static const std::vector<Step> kStrokeThenDot{ { kStroke }, { kTap, 100 } };
            static const std::vector<Step> kQuote{ { kFlick }, { kFlick, 60 } };
            // Chinese and Japanese (full-width) marks follow their Western twins; the full stop and the enumeration comma
            // (U+3002, U+3001) are a small circle and a tick: flicks.
            static const std::unordered_map<wchar_t, std::vector<Step>> kRecipes{
                { L'.', kDot }, { L',', kComma }, { L':', kColon }, { L';', kSemicolon }, { L'!', kStrokeThenDot },
                { L'?', kStrokeThenDot }, { L'\'', kComma }, { L'"', kQuote }, { L'-', kComma },
                { wchar_t(0x3002), kComma }, { wchar_t(0x3001), kComma }, { wchar_t(0xFF0C), kComma },
                { wchar_t(0xFF1A), kColon }, { wchar_t(0xFF1B), kSemicolon }, { wchar_t(0xFF01), kStrokeThenDot },
                { wchar_t(0xFF1F), kStrokeThenDot },
            };
            return kRecipes;
        }

        // UI thread only: Editor's Type, and Tick from the book menu's frame (they run on the same thread).
        using Clock = std::chrono::steady_clock;
        struct Due
        {
            Clock::time_point at;
            std::function<void()> work;
        };
        std::vector<Due> g_due;  // later sounds and flick cuts, played by Tick
        RE::BSSoundHandle g_last;
        std::optional<Clock::time_point> g_lastStart;
        unsigned g_typed = 0;  // counts Play calls: a mark's later steps are dropped once something else is typed

        RE::BGSSoundDescriptorForm* Find(RE::FormID id)
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            auto* form = data ? data->LookupForm<RE::BGSSoundDescriptorForm>(id, kPlugin) : nullptr;
            if (!form) SKSE::log::warn("[Sound] No writing sound: {:X} not found in {}", id, kPlugin);
            return form;
        }

        // Work `ms` from now, at the first frame after (Tick).
        void After(int ms, std::function<void()> work) { g_due.push_back({ Clock::now() + std::chrono::milliseconds(ms), std::move(work) }); }

        void Start(Sound sound)
        {
            static auto* scratch = Find(kScratchID);
            static auto* dot = Find(kDotID);
            const bool tap = sound == Sound::kTap;
            auto* descriptor = tap ? dot : scratch;
            auto* audio = RE::BSAudioManager::GetSingleton();
            if (!descriptor || !audio) return;
            // A tap's hit is its first milliseconds: a scratch still fading out over them buried it, so it stops at once.
            if (g_last.IsValid() && g_last.IsPlaying()) tap ? g_last.Stop() : g_last.FadeOutAndRelease(kCutMs);
            // A tap plays twice at once, about 6 dB over its file (as loud as a file goes; a volume over 1 did nothing).
            // Its record has no pitch variance, so the copies match (docs/EDITOR.md#the-writing-sound).
            RE::BSSoundHandle handle;
            for (int copies = tap ? 2 : 1; copies > 0; --copies) {
                if (!audio->GetSoundHandle(handle, descriptor) || !handle.Play()) {
                    SKSE::log::debug("[Sound] The engine didn't play a {}", tap ? "tap" : "scratch");
                    return;
                }
            }
            g_last = handle;
            g_lastStart = Clock::now();
            if (sound == Sound::kFlick) {
                // Cut short, unless something else has taken over (its id may be reused once it's released).
                After(kFlickMs, [handle]() mutable {
                    if (g_last.soundID == handle.soundID && handle.IsPlaying()) handle.FadeOutAndRelease(kFlickFadeMs);
                });
            }
        }

        void Run(const std::vector<Step>& steps, std::size_t at, unsigned typed)
        {
            Start(steps[at].sound);
            if (at + 1 >= steps.size()) return;
            After(steps[at + 1].afterMs, [&steps, at, typed]() {
                if (typed == g_typed) Run(steps, at + 1, typed);
            });
        }

    }

    void Play(std::string_view text)
    {
        const unsigned typed = ++g_typed;
        const auto wide = Strings::Wide(text);
        if (wide.size() == 1) {
            if (const auto recipe = Recipes().find(wide[0]); recipe != Recipes().end()) {
                Run(recipe->second, 0, typed);  // a mark always sounds
                return;
            }
        }
        if (g_lastStart && Clock::now() - *g_lastStart < kMinGap) return;
        Start(Sound::kStroke);
    }

    void Tick()
    {
        if (g_due.empty()) return;
        const auto now = Clock::now();
        std::vector<Due> ready;  // taken out first: running one can add the next step
        std::erase_if(g_due, [&](Due& due) {
            if (due.at > now) return false;
            ready.push_back(std::move(due));
            return true;
        });
        for (auto& due : ready) due.work();
    }

    void Clear() { g_due.clear(); }

}
