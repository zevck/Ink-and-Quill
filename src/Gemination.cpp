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

#include "Gemination.h"

#include "Settings.h"
#include "Strings.h"
#include "WritingTools.h"

#include <Windows.h>

namespace InkAndQuill::Gemination {

    namespace {

        constexpr std::string_view kPlugin = "InkAndQuill.esp";
        constexpr RE::FormID kSpecialInk = 0x807;  // InkAndQuillSpecialInk
        constexpr RE::FormID kInkForSale = 0x809;  // InkAndQuillLItemSpecialInk: one Special Ink, for Enthir's chests

        // Skyrim.esm (load order 00, so these are full FormIDs).
        constexpr RE::FormID kQuill = 0x0C04BB;      // FVDQuill, the Quill of Gemination
        constexpr RE::FormID kValdsDebt = 0x0C04BA;  // FreeformValdDebt: stage 40, the quill handed to Maven (destroyed)
        constexpr std::uint16_t kHandedOver = 40;    // (Scoundrel's Folly later stops it at 200)
        constexpr RE::FormID kMaven = 0x019DD1;      // MavenRef
        // Enthir's vendor factions: the College's, and the fence's (TGFences adds it; his barter then shows its chest).
        constexpr RE::FormID kEnthirFactions[] = { 0x0EE9F6, 0x0D8844 };

        // Snazzy Location Resources by gutmaw: its Special Ink and the recipe for it; its Riften interiors place both.
        constexpr std::string_view kSnazzy = "Snazzy Location Resources.esp";
        constexpr std::string_view kSnazzyInkId = "FVD_SpecialInk01";
        constexpr RE::FormID kSnazzyInk = 0x05CE2A;
        constexpr std::string_view kSnazzyNoteId = "GM_RecipeSpecialInkNOTE01";
        constexpr RE::FormID kSnazzyNote = 0x05CE28;

        // The FOMOD's "Quill of Gemination" choice (docs/EDITOR.md#special-ink).
        enum class Snazzy { None, Resources, Riften };
        Snazzy g_snazzy = Snazzy::None;

        RE::TESBoundObject* g_quill = nullptr;
        RE::TESBoundObject* g_ink = nullptr;  // the Special Ink in use: ours, or Snazzy's
        RE::TESObjectMISC* g_ownInk = nullptr;
        RE::TESBoundObject* g_note = nullptr;  // Snazzy's recipe, with Snazzy::Resources
        bool g_onMaven = false;               // this save's Maven has had the quill (the co-save)

        // A record of Snazzy's: by EditorID (kept with powerofthree's Tweaks), else by FormID.  Not
        // LookupForm<TESBoundObject>: it compares form types, and that base class has none.
        RE::TESBoundObject* SnazzyForm(RE::TESDataHandler* data, std::string_view editorId, RE::FormID id)
        {
            auto* form = RE::TESForm::LookupByEditorID(editorId);
            if (!form) form = data->LookupForm(id, kSnazzy);
            return form ? form->As<RE::TESBoundObject>() : nullptr;
        }

        Snazzy ReadChoice()
        {
            for (const auto& file : Settings::IniFiles()) {
                char text[64] = {};
                GetPrivateProfileStringA("QuillOfGemination", "Snazzy", "", text, sizeof(text), file.c_str());
                if (_stricmp(text, "Resources") == 0) return Snazzy::Resources;
                if (_stricmp(text, "Riften") == 0) return Snazzy::Riften;
                if (*text) SKSE::log::warn("[Gemination] {}: Snazzy = \"{}\" isn't Resources or Riften", file, text);
            }
            return Snazzy::None;
        }

        // The FOMOD's choice decides.  Only an ink that can't be found (Snazzy removed since) falls back to ours.
        void Resolve(RE::TESDataHandler* data)
        {
            g_snazzy = ReadChoice();
            if (g_snazzy != Snazzy::None && !SnazzyForm(data, kSnazzyInkId, kSnazzyInk)) {
                SKSE::log::error("[Gemination] The FOMOD chose Snazzy, but {} has no Special Ink ({} or {:06X}): Ink & Quill's own instead",
                                 kSnazzy, kSnazzyInkId, kSnazzyInk);
                g_snazzy = Snazzy::None;
            }
        }

        int Count(RE::TESBoundObject* item) { return WritingTools::Count(RE::PlayerCharacter::GetSingleton(), item); }

        // The quill (with Snazzy Location Resources also its recipe) on Maven, once per save: the hand-over destroys the
        // player's.  Not with Snazzy's Riften interiors (they place both), nor to a player who still has one.
        void PutQuillOnMaven()
        {
            try {
                auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(kValdsDebt);
                auto* maven = RE::TESForm::LookupByID<RE::TESObjectREFR>(kMaven);
                if (g_snazzy == Snazzy::Riften || g_onMaven || !quest || !maven || !g_quill) return;
                if (quest->GetCurrentStageID() < kHandedOver || Count(g_quill) > 0) return;
                maven->AddObjectToContainer(g_quill, nullptr, 1, nullptr);
                if (g_note) maven->AddObjectToContainer(g_note, nullptr, 1, nullptr);
                g_onMaven = true;
                SKSE::log::info("[Gemination] The Quill of Gemination{} put on Maven", g_note ? " and Snazzy's recipe" : "");
            } catch (const std::exception& e) {
                SKSE::log::error("[Gemination] Putting the quill on Maven failed: {}", e.what());
            }
        }

        // Vald's Debt advancing: the quill on Maven, on the game thread.
        class StageSink : public RE::BSTEventSink<RE::TESQuestStageEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStageEvent* a_event,
                                                  RE::BSTEventSource<RE::TESQuestStageEvent>*) override
            {
                if (a_event && a_event->formID == kValdsDebt && a_event->stage >= kHandedOver) {
                    SKSE::GetTaskInterface()->AddTask(PutQuillOnMaven);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // Enthir's vendor chests get one Special Ink as their own leveled entry, in memory (docs/EDITOR.md#special-ink).
        void StockEnthir(RE::TESDataHandler* data)
        {
            auto* ink = data->LookupForm<RE::TESLevItem>(kInkForSale, kPlugin);
            for (const auto id : kEnthirFactions) {
                auto* chest = WritingTools::MerchantChest(RE::TESForm::LookupByID<RE::TESFaction>(id));
                if (!chest || !ink || chest->GetObjectCount(ink) > 0) continue;
                if (WritingTools::AddToChest(chest, ink)) {
                    SKSE::log::info("[Gemination] Enthir sells Special Ink: chest {:08X} (vendor faction {:08X})", chest->GetFormID(), id);
                }
            }
        }

    }

    void OnDataLoaded(bool writingOn)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) return;
        g_quill = RE::TESForm::LookupByID<RE::TESBoundObject>(kQuill);
        g_ownInk = data->LookupForm<RE::TESObjectMISC>(kSpecialInk, kPlugin);
        if (g_ownInk) g_ownInk->fullName = Strings::Get("$IQ_SpecialInkName").c_str();
        Resolve(data);
        g_ink = g_snazzy != Snazzy::None ? SnazzyForm(data, kSnazzyInkId, kSnazzyInk) : g_ownInk;
        g_note = g_snazzy == Snazzy::Resources ? SnazzyForm(data, kSnazzyNoteId, kSnazzyNote) : nullptr;
        if (!g_quill || !g_ink) SKSE::log::error("[Gemination] No Quill of Gemination or Special Ink record: no Special Ink");
        SKSE::log::info("[Gemination] Special Ink: {}", g_snazzy == Snazzy::None ? "Ink & Quill's, sold by Enthir"
                                                       : g_snazzy == Snazzy::Resources ? "Snazzy's; quill and recipe on Maven"
                                                                                       : "Snazzy's; quill and recipe in Black-Briar Manor");
        ApplyPrice();
        if (writingOn && g_snazzy == Snazzy::None) StockEnthir(data);
        static StageSink stageSink;
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) events->AddEventSink<RE::TESQuestStageEvent>(&stageSink);
    }

    void OnGameLoaded() { PutQuillOnMaven(); }

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t type)
    {
        const std::uint8_t onMaven = g_onMaven;
        if (!a_intfc->OpenRecord(type, 1) || !a_intfc->WriteRecordData(onMaven)) SKSE::log::error("[Gemination] Couldn't save");
    }

    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t version)
    {
        std::uint8_t onMaven = 0;
        if (version == 1 && a_intfc->ReadRecordData(onMaven)) g_onMaven = onMaven != 0;
    }

    void Revert() { g_onMaven = false; }

    void ApplyPrice()
    {
        if (g_ownInk) g_ownInk->value = Settings::Get(Settings::kSpecialInkPrice);
    }

    bool SoldByEnthir() { return g_snazzy == Snazzy::None; }

    bool HasQuill() { return Count(g_quill) > 0; }

    bool HasInk() { return Count(g_ink) > 0; }

    RE::FormID UseInk()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !HasInk()) return 0;
        player->RemoveItem(g_ink, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
        SKSE::log::info("[Gemination] Used a Special Ink");
        return g_ink->GetFormID();
    }

    RE::TESBoundObject* Quill() { return g_quill; }

}
