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

#include <charconv>
#include <cstring>

namespace InkAndQuill::WritingTools {

    namespace {

        // Saves a full inkwell lasts.
        constexpr int kUses = 10;

        // Writing in blood: this share of the player's maximum health per save, never below kMinHealthAfterBleeding.
        constexpr float kBloodCost = 0.1f;
        constexpr float kMinHealthAfterBleeding = 1.0f;

        constexpr std::string_view kPlugin = "InkAndQuill.esp";
        constexpr RE::FormID kQuillsList = 0x800;    // InkAndQuillQuills
        constexpr RE::FormID kInkwellsList = 0x801;  // InkAndQuillInkwells

        RE::BGSListForm* g_quills = nullptr;
        RE::BGSListForm* g_inkwells = nullptr;

        int Count(RE::PlayerCharacter* player, RE::TESBoundObject* item)
        {
            return item ? player->GetInventoryCounts([item](RE::TESBoundObject& i) { return &i == item; })[item] : 0;
        }

        // Any form in the list the player carries.
        bool CarriesAny(RE::PlayerCharacter* player, RE::BGSListForm* list)
        {
            if (!player || !list) return false;
            bool found = false;
            list->ForEachForm([&](RE::TESForm* form) {
                found = form && Count(player, form->As<RE::TESBoundObject>()) > 0;
                return found ? RE::BSContainer::ForEachResult::kStop : RE::BSContainer::ForEachResult::kContinue;
            });
            return found;
        }

        // The uses left in a used inkwell's name, "Inkwell (n/10)"; 0 for a full one.
        int UsesLeft(RE::ExtraDataList* list)
        {
            auto* text = list ? list->GetByType<RE::ExtraTextDisplayData>() : nullptr;
            if (!text || !text->IsPlayerSet()) return 0;
            const std::string_view name = text->displayName.c_str();
            const std::string suffix = std::format("/{})", kUses);
            const auto open = name.rfind('(');
            if (!name.ends_with(suffix) || open == std::string_view::npos) return 0;
            const char* last = name.data() + name.size() - suffix.size();
            int uses = 0;
            const auto [end, error] = std::from_chars(name.data() + open + 1, last, uses);
            return error == std::errc() && end == last && uses > 0 && uses < kUses ? uses : 0;
        }

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
            Inkwell used{ .uses = kUses };
            Inkwell full;
            auto* changes = player->GetInventoryChanges();
            if (!changes || !changes->entryList || !g_inkwells) return full;
            for (auto* entry : *changes->entryList) {
                if (!entry || !entry->object || !g_inkwells->HasForm(entry->object)) continue;
                int inLists = 0;
                if (entry->extraLists) {
                    for (auto* list : *entry->extraLists) {
                        if (!list) continue;
                        inLists += list->GetCount();
                        const int uses = UsesLeft(list);
                        if (uses > 0 && uses < used.uses) used = { entry, list, uses };
                        else if (uses == 0 && !full.entry) full = { entry, list, kUses };
                    }
                }
                // Plain ones first: an extra list may carry ownership or a favourite.
                if (Count(player, entry->object) > inLists) full = { entry, nullptr, kUses };
            }
            return used.entry ? used : full;
        }

    }

    void OnDataLoaded()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        g_quills = data ? data->LookupForm<RE::BGSListForm>(kQuillsList, kPlugin) : nullptr;
        g_inkwells = data ? data->LookupForm<RE::BGSListForm>(kInkwellsList, kPlugin) : nullptr;
        if (!g_quills || !g_inkwells) SKSE::log::error("[WritingTools] {}'s quill or inkwell list is missing: nobody can write", kPlugin);
    }

    bool HasQuill() { return CarriesAny(RE::PlayerCharacter::GetSingleton(), g_quills); }

    bool HasInk() { return CarriesAny(RE::PlayerCharacter::GetSingleton(), g_inkwells); }

    Ink UseInk()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto inkwell = player ? Emptiest(player) : Inkwell{};
        if (!inkwell.entry) return Ink::None;
        auto* object = inkwell.entry->object;
        const int left = inkwell.uses - 1;
        if (left == 0) {
            player->RemoveItem(object, 1, RE::ITEM_REMOVE_REASON::kRemove, inkwell.list, nullptr);
            SKSE::log::info("[WritingTools] An inkwell ran dry");
            return Ink::RanDry;
        }
        const std::string name = std::format("{} ({}/{})", object->GetName(), left, kUses);
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
        const float cost = health->GetPermanentActorValue(RE::ActorValue::kHealth) * kBloodCost;
        return health->GetActorValue(RE::ActorValue::kHealth) - cost >= kMinHealthAfterBleeding;
    }

    bool Bleed()
    {
        if (!CanBleed()) {
            SKSE::log::info("[WritingTools] Too weak to write in blood");
            return false;
        }
        auto* health = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner();
        const float cost = health->GetPermanentActorValue(RE::ActorValue::kHealth) * kBloodCost;
        health->DamageActorValue(RE::ActorValue::kHealth, cost);
        SKSE::log::info("[WritingTools] Bled {:.0f} health", cost);
        return true;
    }

}
