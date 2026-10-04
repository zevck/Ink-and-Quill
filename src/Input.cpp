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

#include "Input.h"

#include "Editor.h"
#include "Keys.h"
#include "Settings.h"

namespace InkAndQuill::Input {

    namespace {
        using namespace Keys;

        // Keys clients handle while the player writes (SetClientKeys): left in the input, not read as text.  The
        // flags for the input thread; each client's set (by its id) to rebuild them when one changes its keys.
        std::array<std::atomic<bool>, 256> g_clientKeys{};
        std::mutex g_clientKeysLock;
        std::map<const void*, std::vector<std::uint32_t>> g_keysByClient;

        bool IsClientKey(std::uint32_t code) { return code < g_clientKeys.size() && g_clientKeys[code]; }

        // Reads the keyboard events for the editor, then takes them out of the list, so nothing after it sees them:
        // the book menu, other mods' sinks and dispatch hooks (docs/EDITOR.md#input).  Input thread: work is queued.
        void FilterInput(RE::InputEvent** events)
        {
            if (!events || !*events || Editor::IsPrompting()) return;  // a prompt's keys are its own
            const bool writing = Editor::IsWriting();
            if (!writing && !Editor::IsBookOpen()) return;
            bool editKeyTaken = false;
            for (auto* event = *events; event; event = event->next) {
                auto* button = event->AsButtonEvent();
                if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;
                const auto code = button->GetIDCode();
                if (!writing) {
                    // The edit key while a book is open: write in it, if a client owns it.
                    if (code == Settings::EditKey() && button->IsDown()) {
                        editKeyTaken = true;
                        Editor::OnEditKey(writing);
                    }
                    continue;
                }
                if (code == Settings::EditKey()) {
                    if (button->IsDown()) Editor::OnEditKey(writing);
                    continue;
                }
                if (IsClientKey(code) || IsModifier(code)) continue;
                if (Keys::ShouldRepeat(button, code)) Editor::OnKey(code);
            }
            if (!writing && !editKeyTaken) return;
            // Unlink what nobody else may see: while writing every keyboard event but clients' keys, else the edit key.
            RE::InputEvent* kept = nullptr;
            RE::InputEvent** tail = &kept;
            for (auto* event = *events; event;) {
                auto* next = event->next;
                auto* button = event->AsButtonEvent();
                const bool keyboard = button && button->GetDevice() == RE::INPUT_DEVICE::kKeyboard;
                const auto code = keyboard ? button->GetIDCode() : 0;
                const bool drop = keyboard && (writing ? code == Settings::EditKey() || !IsClientKey(code) : code == Settings::EditKey());
                if (!drop) {
                    event->next = nullptr;
                    *tail = event;
                    tail = &event->next;
                }
                event = next;
            }
            *events = kept;
        }

        // Layer 1: first in line among the input sinks (prepended), ahead of MenuControls and SKSE's Papyrus key events.
        class InputSink : public RE::BSTEventSink<RE::InputEvent*> {
        public:
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                                  RE::BSTEventSource<RE::InputEvent*>*) override
            {
                FilterInput(const_cast<RE::InputEvent**>(a_event));
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // Layer 2: the engine's input dispatch, the call Wheeler hooks (write_call), hooked after it so ours runs first.
        struct InputDispatch {
            static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_source, RE::InputEvent** a_events)
            {
                FilterInput(a_events);
                func(a_source, a_events);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    void Register()
    {
        static InputSink inputSink;
        // First in line, ahead of MenuControls and SKSE's Papyrus key events.
        if (auto* input = RE::BSInputDeviceManager::GetSingleton()) input->PrependEventSink(&inputSink);
    }

    int SetClientKeys(const void* client, const std::vector<std::uint32_t>& codes)
    {
        std::vector<std::uint32_t> kept;
        for (const auto code : codes) {
            if (Keys::Check(code) != Keys::Problem::None || code == Settings::EditKey()) {
                SKSE::log::warn("[Input] Key 0x{:X} refused as a client's key: it types, edits or is the edit key", code);
                continue;
            }
            kept.push_back(code);
        }
        std::scoped_lock lock(g_clientKeysLock);
        g_keysByClient[client] = kept;
        for (auto& flag : g_clientKeys) flag = false;
        for (const auto& [id, keys] : g_keysByClient) {
            for (const auto code : keys) g_clientKeys[code] = true;
        }
        SKSE::log::info("[Input] A client's keys while writing: {} of {} kept", kept.size(), codes.size());
        return static_cast<int>(kept.size());
    }

    void InstallHook()
    {
        static bool installed = false;
        if (std::exchange(installed, true)) return;
        SKSE::AllocTrampoline(14);
        REL::Relocation<std::uintptr_t> dispatch{ RELOCATION_ID(67315, 68617), REL::Relocate(0x7B, 0x7B, 0x81) };
        InputDispatch::func = SKSE::GetTrampoline().write_call<5>(dispatch.address(), InputDispatch::thunk);
        SKSE::log::info("[Input] Input dispatch hooked (after Wheeler's, so ours runs first)");
    }

}
