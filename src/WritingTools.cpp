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

#include "WritingTools.h"

#include "Settings.h"

#include <charconv>
#include <cstring>
#include <sstream>
#include <unordered_set>

#include <Windows.h>

namespace InkAndQuill::WritingTools {

    namespace {

        // Writing in blood never leaves the player below this; its cost is Settings::kBloodCost.
        constexpr float kMinHealthAfterBleeding = 1.0f;

        int MaxUses() { return Settings::Get(Settings::kInkwellUses); }

        float BloodCost()
        {
            auto* health = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner();
            return health->GetPermanentActorValue(RE::ActorValue::kHealth) * Settings::Get(Settings::kBloodCost) / 100.0f;
        }

        constexpr std::string_view kPlugin = "InkAndQuill.esp";
        constexpr RE::FormID kQuillsList = 0x800;    // InkAndQuillQuills
        constexpr RE::FormID kInkwellsList = 0x801;  // InkAndQuillInkwells

        RE::BGSListForm* g_quills = nullptr;
        RE::BGSListForm* g_inkwells = nullptr;
        // Other mods' quills and inkwells, from the INI files ([Materials]): kept here, not added to the form lists
        // (an added form would stay in the player's save after it's taken out of the INI).
        std::unordered_set<RE::FormID> g_extraQuills, g_extraInkwells;

        bool IsQuill(const RE::TESForm* form) { return form && ((g_quills && g_quills->HasForm(form)) || g_extraQuills.contains(form->GetFormID())); }
        bool IsInkwell(const RE::TESForm* form) { return form && ((g_inkwells && g_inkwells->HasForm(form)) || g_extraInkwells.contains(form->GetFormID())); }

        // "0x123456~Plugin.esp, 0xABC~Other.esl": each an item the player can carry, added to set.
        void ReadMaterials(const std::string& path, const char* key, std::unordered_set<RE::FormID>& set)
        {
            char text[4096] = {};
            GetPrivateProfileStringA("Materials", key, "", text, sizeof(text), path.c_str());
            std::istringstream in(text);
            const auto trim = [](std::string_view s) {
                while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
                while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
                return std::string(s);
            };
            auto* data = RE::TESDataHandler::GetSingleton();
            for (std::string entry; std::getline(in, entry, ',');) {
                const auto item = trim(entry);
                if (item.empty()) continue;
                const auto tilde = item.find('~');
                const auto digits = trim(std::string_view(item).substr(0, tilde));
                const auto plugin = tilde == std::string::npos ? std::string() : trim(std::string_view(item).substr(tilde + 1));
                RE::FormID id = 0;
                const bool hex = digits.starts_with("0x") || digits.starts_with("0X");
                const auto [end, error] = std::from_chars(digits.data() + (hex ? 2 : 0), digits.data() + digits.size(), id, 16);
                const auto* file = data && !plugin.empty() ? data->LookupModByName(plugin) : nullptr;
                // An ID copied with its load-order byte: only the plugin's own part counts.
                if (file) id &= file->IsLight() ? 0xFFF : 0xFFFFFF;
                auto* form = !file || error != std::errc() || end != digits.data() + digits.size() ? nullptr : data->LookupForm(id, plugin);
                if (!form || !form->IsBoundObject()) {
                    SKSE::log::warn("[WritingTools] {} {}: \"{}\" isn't an item in a loaded plugin (\"0x123~Plugin.esp\")", path, key, item);
                    continue;
                }
                set.insert(form->GetFormID());
            }
        }

        int Count(RE::PlayerCharacter* player, RE::TESBoundObject* item)
        {
            return item ? player->GetInventoryCounts([item](RE::TESBoundObject& i) { return &i == item; })[item] : 0;
        }

        // Any such item the player carries.
        bool CarriesAny(RE::PlayerCharacter* player, bool (*is)(const RE::TESForm*))
        {
            auto* changes = player ? player->GetInventoryChanges() : nullptr;
            if (!changes || !changes->entryList) return false;
            for (auto* entry : *changes->entryList) {
                if (entry && is(entry->object) && Count(player, entry->object) > 0) return true;
            }
            return false;
        }

        // A used inkwell's name: the record's name and "(uses left/maximum)", which NamedUses reads back.
        std::string InkwellName(RE::TESBoundObject* object, int left) { return std::format("{} ({}/{})", object->GetName(), left, MaxUses()); }

        // A used inkwell's name, "Inkwell (n/m)": n and m, or nothing (a full one).
        std::optional<std::pair<int, int>> NamedUses(RE::ExtraDataList* list)
        {
            auto* text = list ? list->GetByType<RE::ExtraTextDisplayData>() : nullptr;
            if (!text || !text->IsPlayerSet()) return std::nullopt;
            const std::string_view name = text->displayName.c_str();
            const auto open = name.rfind('(');
            const auto slash = name.rfind('/');
            if (!name.ends_with(")") || open == std::string_view::npos || slash == std::string_view::npos || slash < open) return std::nullopt;
            int uses = 0, of = 0;
            const char* end = name.data() + name.size() - 1;
            const auto [usesEnd, usesError] = std::from_chars(name.data() + open + 1, name.data() + slash, uses);
            const auto [ofEnd, ofError] = std::from_chars(name.data() + slash + 1, end, of);
            if (usesError != std::errc() || ofError != std::errc() || usesEnd != name.data() + slash || ofEnd != end) return std::nullopt;
            if (uses <= 0 || of <= 0) return std::nullopt;
            return std::pair{ uses, of };
        }

        // The uses left in a used inkwell, at most the current maximum (the setting may have changed since it was
        // named); 0 for a full one.
        int UsesLeft(RE::ExtraDataList* list)
        {
            const auto named = NamedUses(list);
            return named ? std::min(named->first, MaxUses()) : 0;
        }

        // Used inkwells named against another maximum, renamed to the current one ("(7/10)" with 20: "(7/20)"; with
        // 5: "(5/5)").  With 0 (never run dry) they're left alone.  Game thread.
        bool RenameStale(RE::TESObjectREFR* ref)
        {
            auto* changes = ref && MaxUses() > 0 ? ref->GetInventoryChanges() : nullptr;
            if (!changes || !changes->entryList) return false;
            bool renamed = false;
            for (auto* entry : *changes->entryList) {
                if (!entry || !entry->object || !entry->extraLists || !IsInkwell(entry->object)) continue;
                for (auto* list : *entry->extraLists) {
                    const auto named = NamedUses(list);
                    if (!named || (named->second == MaxUses() && named->first <= MaxUses())) continue;
                    const auto name = InkwellName(entry->object, std::min(named->first, MaxUses()));
                    list->SetOverrideName(name.c_str());
                    renamed = true;
                }
            }
            if (renamed) SKSE::log::info("[WritingTools] Renamed used inkwells to the current maximum ({:08X})", ref->GetFormID());
            return renamed;
        }

        // An inventory, container, shop or gift menu opening: the player's and the container's inkwells renamed, and
        // the menu's list refreshed.
        class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                                  RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (!a_event || !a_event->opening) return RE::BSEventNotifyControl::kContinue;
                const auto& menu = a_event->menuName;
                RE::RefHandle target = 0;
                if (menu == RE::ContainerMenu::MENU_NAME) {
                    target = RE::ContainerMenu::GetTargetRefHandle();
                } else if (menu != RE::InventoryMenu::MENU_NAME && menu != RE::BarterMenu::MENU_NAME &&
                           menu != RE::GiftMenu::MENU_NAME) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                SKSE::GetTaskInterface()->AddTask([target]() {
                    try {
                        auto* player = RE::PlayerCharacter::GetSingleton();
                        if (RenameStale(player)) RE::SendUIMessage::SendInventoryUpdateMessage(player, nullptr);
                        RE::TESObjectREFRPtr container;
                        if (target != 0 && RE::LookupReferenceByHandle(target, container) && container && container.get() != player &&
                            RenameStale(container.get())) {
                            RE::SendUIMessage::SendInventoryUpdateMessage(container.get(), nullptr);
                        }
                    } catch (const std::exception& e) {
                        SKSE::log::error("[WritingTools] Renaming inkwells failed: {}", e.what());
                    } catch (...) {
                        SKSE::log::error("[WritingTools] Renaming inkwells failed");
                    }
                });
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // An empty item extra list as the engine lays one out (CommonLib has no constructor): zeroed,
        // the presence bits on the game heap, and on AE 1.6.629+ the vtable of the player's own list.
        RE::ExtraDataList* NewExtraList()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const bool hasVtable = REL::Module::IsAE() && REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_6_629;
            auto* list = static_cast<std::byte*>(RE::malloc(0x20));
            auto* presence = RE::calloc<RE::BaseExtraList::PresenceBitfield>(1);
            if (!player || !list || !presence) return nullptr;
            std::memset(list, 0, 0x20);
            if (hasVtable) std::memcpy(list, &player->extraList, sizeof(std::uintptr_t));
            *reinterpret_cast<void**>(list + (hasVtable ? 0x10 : 0x08)) = presence;
            return reinterpret_cast<RE::ExtraDataList*>(list);
        }

        // One inkwell: its entry, its extra list (none for a plain one), its uses left.
        struct Inkwell {
            RE::InventoryEntryData* entry = nullptr;
            RE::ExtraDataList* list = nullptr;
            int uses = 0;
        };

        // The player's emptiest inkwell, or a full one.
        Inkwell Emptiest(RE::PlayerCharacter* player)
        {
            Inkwell used{ .uses = MaxUses() + 1 };
            Inkwell full;
            auto* changes = player->GetInventoryChanges();
            if (!changes || !changes->entryList) return full;
            for (auto* entry : *changes->entryList) {
                if (!entry || !IsInkwell(entry->object)) continue;
                int inLists = 0;
                if (entry->extraLists) {
                    for (auto* list : *entry->extraLists) {
                        if (!list) continue;
                        inLists += list->GetCount();
                        const int uses = UsesLeft(list);
                        if (uses > 0 && uses < used.uses) used = { entry, list, uses };
                        else if (uses == 0 && !full.entry) full = { entry, list, MaxUses() };
                    }
                }
                // Plain ones first: an extra list may carry ownership or a favourite.
                if (Count(player, entry->object) > inLists) full = { entry, nullptr, MaxUses() };
            }
            return used.entry ? used : full;
        }

    }

    void OnDataLoaded()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        g_quills = data ? data->LookupForm<RE::BGSListForm>(kQuillsList, kPlugin) : nullptr;
        g_inkwells = data ? data->LookupForm<RE::BGSListForm>(kInkwellsList, kPlugin) : nullptr;
        if (!g_quills || !g_inkwells) SKSE::log::error("[WritingTools] {}'s quill or inkwell list is missing", kPlugin);
        // Ink & Quill's own INI, then each mod's file in SKSE/Plugins/InkAndQuill/.
        std::vector<std::string> files{ Settings::IniPath() };
        std::error_code ec;
        for (const auto& file : std::filesystem::directory_iterator(Settings::kModFilesFolder, ec)) {
            if (file.path().extension() == ".ini") files.push_back(std::filesystem::absolute(file.path()).string());
        }
        for (const auto& file : files) {
            ReadMaterials(file, "Quills", g_extraQuills);
            ReadMaterials(file, "Inkwells", g_extraInkwells);
        }
        SKSE::log::info("[WritingTools] Other quills: {}, other inkwells: {}", g_extraQuills.size(), g_extraInkwells.size());
        static MenuSink menuSink;
        if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuSink);
    }

    bool HasQuill() { return CarriesAny(RE::PlayerCharacter::GetSingleton(), IsQuill); }

    bool HasInk() { return CarriesAny(RE::PlayerCharacter::GetSingleton(), IsInkwell); }

    Ink UseInk()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto inkwell = player ? Emptiest(player) : Inkwell{};
        if (!inkwell.entry) return Ink::None;
        if (MaxUses() == 0) return Ink::Used;  // inkwells never run dry: none is used or renamed
        auto* object = inkwell.entry->object;
        const int left = inkwell.uses - 1;
        if (left == 0) {
            player->RemoveItem(object, 1, RE::ITEM_REMOVE_REASON::kRemove, inkwell.list, nullptr);
            SKSE::log::info("[WritingTools] An inkwell ran dry");
            return Ink::RanDry;
        }
        const std::string name = InkwellName(object, left);
        if (inkwell.list && inkwell.list->GetCount() == 1) {
            inkwell.list->SetOverrideName(name.c_str());
        } else {
            // Split one off its stack: a plain one, or one of several alike in one list.
            auto* list = NewExtraList();
            if (!list) {
                SKSE::log::error("[WritingTools] Couldn't make an extra list: no ink used");
                return Ink::Used;
            }
            if (inkwell.list) inkwell.list->SetCount(static_cast<std::uint16_t>(inkwell.list->GetCount() - 1));
            list->SetOverrideName(name.c_str());
            inkwell.entry->AddExtraList(list);
        }
        SKSE::log::info("[WritingTools] Used ink: \"{}\"", name);
        return Ink::Used;
    }

    bool CanBleed()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* health = player ? player->AsActorValueOwner() : nullptr;
        if (!health) return false;
        return health->GetActorValue(RE::ActorValue::kHealth) - BloodCost() >= kMinHealthAfterBleeding;
    }

    bool Bleed()
    {
        if (!CanBleed()) {
            SKSE::log::info("[WritingTools] Too weak to write in blood");
            return false;
        }
        auto* health = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner();
        const float cost = BloodCost();
        health->DamageActorValue(RE::ActorValue::kHealth, cost);
        SKSE::log::info("[WritingTools] Bled {:.0f} health", cost);
        return true;
    }

}
