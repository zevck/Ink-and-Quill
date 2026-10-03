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

#include "Editor.h"

#include "Settings.h"
#include "Strings.h"
#include "WritingMode.h"
#include "WritingTools.h"

#include <Windows.h>
#include <chrono>

namespace InkAndQuill::Editor {

    namespace {
        // ---- Keys (DirectX scan codes) ----
        constexpr std::uint32_t kEscape = 0x01, kBackspace = 0x0E, kEnter = 0x1C, kDelete = 0xD3;
        constexpr std::uint32_t kLeft = 0xCB, kRight = 0xCD, kUp = 0xC8, kDown = 0xD0, kHome = 0xC7, kEnd = 0xCF;
        // Modifiers only change other keys: Shift, Ctrl, Alt (left and right), Caps Lock.
        constexpr std::uint32_t kModifiers[] = { 0x2A, 0x36, 0x1D, 0x9D, 0x38, 0xB8, 0x3A };

        // Held-key repeat, like a text box (input thread only).
        constexpr auto kKeyRepeatDelay = std::chrono::milliseconds(400);
        constexpr auto kKeyRepeatRate = std::chrono::milliseconds(50);
        std::uint32_t g_lastScanCode = 0;
        std::chrono::steady_clock::time_point g_lastKeyTime{};
        bool g_keyRepeating = false;

        // ---- The session (main thread: the book menu pauses the game) ----

        // An entry in the editor: its text as given or last saved, and whether it was added since.
        struct Edited {
            std::string saved;
            bool added = false;
        };

        std::vector<Owner> g_owners;
        Session g_session;                       // the session writing, or waiting on the blood prompt
        std::vector<Edited> g_edit;              // page order, as the SWF's EditGetBodies
        std::atomic<bool> g_active{ false };     // edit mode is on in the open book menu
        std::atomic<bool> g_prompting{ false };  // a prompt is open over the book: keys belong to it
        bool g_blood = false;                    // this session is in blood (no ink)
        RE::FormID g_beginOnOpen = 0;            // begin g_session once this book is open with its text

        // Blanks (docs/EDITOR.md#blanks).
        std::unordered_map<RE::FormID, Owner> g_blanks;
        RE::FormID g_blankOnOpen = 0;   // a blank read from the inventory: open it once its text is in
        RE::FormID g_openingBlank = 0;  // the blank whose onOpen is running: the session it begins is for it
        RE::FormID g_sessionBlank = 0;  // the blank the session writes in, until a save replaces it

        // The session is over: nothing of it is kept, and the client hears it once (onEnd), last.
        void EndSession()
        {
            auto onEnd = std::move(g_session.client.onEnd);
            g_session = {};
            g_edit.clear();
            g_sessionBlank = 0;
            if (onEnd) onEnd();
        }

        void Notify(const std::string& text) { RE::SendHUDMessage::ShowHUDMessage(text.c_str()); }

        // book.swf is its own movie, separate from bookmenu.swf.
        RE::GFxMovieView* BookMovie()
        {
            auto* ui = RE::UI::GetSingleton();
            auto bookMenu = ui ? ui->GetMenu<RE::BookMenu>() : nullptr;
            return bookMenu ? bookMenu->GetRuntimeData().book.get() : nullptr;
        }

        void Invoke(const char* function, const char* argument = nullptr)
        {
            auto* movie = BookMovie();
            if (!movie) return;
            RE::GFxValue arg;
            if (argument) arg.SetString(argument);
            movie->Invoke(std::format("_root.BookMenu_mc.{}", function).c_str(), nullptr, argument ? &arg : nullptr,
                          argument ? 1 : 0);
        }

        // Keys reach the menu blanked (InputSink), so no controls need turning off; the mouse
        // keeps the book's own page turns (left click previous, right click next).
        void SetTextInput(bool enabled)
        {
            if (auto* controls = RE::ControlMap::GetSingleton()) controls->AllowTextInput(enabled);
        }

        // ---- Prompts over the book ----

        void ShowPrompt(const std::string& body, std::initializer_list<std::string_view> buttons,
                        std::int32_t cancelButton, RE::IMessageBoxCallback* callback)
        {
            auto* data = RE::UIMessageDataFactory::Create<RE::MessageBoxData>();
            if (!data) return;
            data->bodyText = body.c_str();
            for (const auto button : buttons) data->buttonText.push_back(Strings::Get(button).c_str());
            data->cancelButtonIndex = cancelButton;  // Escape on the prompt
            data->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(callback);
            // The prompt's own keys (Enter, Escape) must reach it.
            g_prompting = true;
            SetTextInput(false);
            RE::MessageBoxMenu::QueueMessage(data);
        }

        void EndPrompt()
        {
            g_prompting = false;
            if (g_active) SetTextInput(true);
        }

        // An OK-only message box over the book.
        class NoticeCallback : public RE::IMessageBoxCallback {
        public:
            void Run(std::uint8_t) override { EndPrompt(); }
        };

        void ShowNotice(const std::string& body) { ShowPrompt(body, { "$IQ_Ok" }, 0, new NoticeCallback()); }

        // ---- Loading ----

        // "\r\n" and "\r" to "\n": what a body looks like after the SWF round trip (its field
        // uses "\r"), so an untouched entry compares equal to what was loaded.
        std::string NormalizeLineBreaks(const std::string& text)
        {
            std::string out;
            out.reserve(text.size());
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\r') {
                    out += '\n';
                    if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
                } else {
                    out += text[i];
                }
            }
            return out;
        }

        // The session's document to the SWF (BookMenu.as SetEditContent).
        bool SendContent(RE::GFxMovieView* movie)
        {
            g_edit.clear();
            const auto& doc = g_session.document;
            if (!doc.marked.empty()) {
                // The runs are known once the SWF has taken the markers out (EnterEditMode).
                RE::GFxValue marked[3];
                marked[0].SetString(doc.marked.c_str());
                marked[1].SetString(doc.runFont.c_str());
                marked[2].SetNumber(doc.runSize);
                if (!movie->Invoke("_root.BookMenu_mc.SetEditMarked", nullptr, marked, 3)) {
                    SKSE::log::warn("[Editor] book.swf has no SetEditMarked: it isn't Ink & Quill's (check the load order)");
                    return false;
                }
                return true;
            }
            // "heading\x1Fbody" per entry, joined by \x1E.
            std::string packed;
            for (const auto& entry : doc.entries) {
                Edited edited{ NormalizeLineBreaks(entry.body) };
                if (!packed.empty()) packed += '\x1E';
                packed += entry.heading + '\x1F' + edited.saved;
                g_edit.push_back(std::move(edited));
            }
            RE::GFxValue args[8];
            args[0].SetString(doc.font.c_str());
            args[1].SetNumber(doc.titleSize);
            args[2].SetNumber(doc.smallSize);
            args[3].SetNumber(doc.dateSize);
            args[4].SetNumber(doc.contentSize);
            args[5].SetString(doc.title.c_str());
            args[6].SetString(doc.dates.c_str());
            args[7].SetString(packed.c_str());
            if (!movie->Invoke("_root.BookMenu_mc.SetEditContent", nullptr, args, 8)) {
                SKSE::log::warn("[Editor] book.swf has no SetEditContent: it isn't Ink & Quill's (check the load order)");
                g_edit.clear();
                return false;
            }
            SKSE::log::info("[Editor] {} entries loaded for writing", doc.entries.size());
            return true;
        }

        // ---- The editor's text ----

        // Each entry's text as the editor has it now, in page order; nullopt if the SWF can't
        // say or the count doesn't match g_edit.
        std::optional<std::vector<std::string>> ReadBodies(bool anyCount = false)
        {
            auto* movie = BookMovie();
            RE::GFxValue result;
            if (!movie || !movie->Invoke("_root.BookMenu_mc.EditGetBodies", &result, nullptr, 0) || !result.IsString()) {
                SKSE::log::error("[Editor] No text from the SWF");
                return std::nullopt;
            }
            std::vector<std::string> bodies;
            std::string_view all = result.GetString();
            if (all.empty() && g_edit.empty() && !anyCount) return bodies;  // every entry was torn out
            for (std::size_t start = 0;;) {
                const auto end = all.find('\x1E', start);
                bodies.emplace_back(all.substr(start, end == std::string_view::npos ? all.npos : end - start));
                if (end == std::string_view::npos) break;
                start = end + 1;
            }
            if (!anyCount && bodies.size() != g_edit.size()) {
                SKSE::log::error("[Editor] {} texts from the SWF for {} entries", bodies.size(), g_edit.size());
                return std::nullopt;
            }
            return bodies;
        }

        // true: something to save; false: nothing; nullopt: the editor's text can't be read.
        std::optional<bool> HasChanges()
        {
            if (!g_active) return false;
            const auto bodies = ReadBodies();
            if (!bodies) return std::nullopt;
            for (std::size_t i = 0; i < bodies->size(); ++i) {
                if ((*bodies)[i] != g_edit[i].saved) return true;
            }
            return false;
        }

        // ---- Blanks ----

        int CountCarried(RE::TESBoundObject* item)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player && item ? player->GetInventoryCounts([item](RE::TESBoundObject& i) { return &i == item; })[item] : 0;
        }

        // Points the open book menu at another book: the engine's globals for the base form and the item's extra
        // data list (cleared).  From Physical Diaries; VR's list address was inferred there, never run.
        void SetBookMenuBook(RE::TESObjectBOOK* book)
        {
            static REL::Relocation<RE::ExtraDataList**> extraList{ REL::VariantID(519294, 405834, 0x30111F8) };
            static REL::Relocation<RE::TESObjectBOOK**> menuBook{ REL::VariantID(519295, 405835, 0x3011200) };
            *extraList = nullptr;
            *menuBook = book;
        }

        // One blank goes, and the open menu shows the client's book instead.  False: no such book (the blank stays).
        bool SwapBlank(RE::FormID blankId, RE::FormID bookId)
        {
            auto* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookId);
            if (!book) {
                SKSE::log::warn("[Editor] Blank {:08X}: no book {:08X} to replace it, it stays", blankId, bookId);
                return false;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* blank = RE::TESForm::LookupByID<RE::TESBoundObject>(blankId);
            if (player && blank && CountCarried(blank) > 0) player->RemoveItem(blank, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
            SetBookMenuBook(book);
            SKSE::log::info("[Editor] Blank {:08X} became {:08X}", blankId, bookId);
            return true;
        }

        // The first save of a blank's session, answered with a book.
        void ReplaceBlank(RE::FormID bookId) { SwapBlank(std::exchange(g_sessionBlank, 0), bookId); }

        // A registered blank in the open menu, read from the player's own inventory: in the world, a container, a
        // shop or the gift menu it's just an empty book.
        bool IsOwnBlank(RE::TESObjectBOOK* book)
        {
            auto* ui = RE::UI::GetSingleton();
            return book && ui && g_blanks.contains(book->GetFormID()) && !RE::BookMenu::GetTargetReference() &&
                   !ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME) && !ui->IsMenuOpen(RE::BarterMenu::MENU_NAME) &&
                   !ui->IsMenuOpen(RE::GiftMenu::MENU_NAME) && CountCarried(book) > 0;
        }

        // The blank's client decides: begin a session in it, replace it now (ReplaceOpenBlank), or neither.
        void OpenBlank()
        {
            auto* book = RE::BookMenu::GetTargetForm();
            if (!IsOwnBlank(book) || g_active) return;
            g_openingBlank = book->GetFormID();
            const auto owner = g_blanks[g_openingBlank];
            if (!owner(book)) SKSE::log::info("[Editor] Blank {:08X}: its client didn't begin", book->GetFormID());
            g_openingBlank = 0;
        }

        // The book menu opened: a blank of ours waits for its text (AdvanceMovie), then opens.
        void NoteBlank()
        {
            auto* book = RE::BookMenu::GetTargetForm();
            auto* ui = RE::UI::GetSingleton();
            if (!IsOwnBlank(book)) return;
            if (!ui->GameIsPaused()) {
                Notify(Strings::Get("$IQ_NeedsPause"));
                return;
            }
            g_blankOnOpen = book->GetFormID();
        }

        // ---- Saving ----

        enum class SaveResult { Nothing, Saved, Refused, Unreadable };

        // The client keeps the text; ink or blood is charged once it has (docs/EDITOR.md#saving).
        SaveResult Save(std::string* readText = nullptr)
        {
            const auto bodies = ReadBodies();
            if (!bodies) return SaveResult::Unreadable;
            bool changed = false;
            for (std::size_t i = 0; i < bodies->size(); ++i) changed = changed || (*bodies)[i] != g_edit[i].saved;
            if (!changed) return SaveResult::Nothing;
            if (g_blood ? !WritingTools::CanBleed() : !WritingTools::HasInk()) {
                // Checked when writing began; with the menu pausing the game only health can change.
                ShowNotice(Strings::Get(g_blood ? "$IQ_TooWeak" : "$IQ_NeedsQuill"));
                return SaveResult::Refused;
            }
            auto saved = g_session.client.onSave ? g_session.client.onSave(*bodies) : Saved{};
            if (!saved.accepted) {
                SKSE::log::info("[Editor] The client refused the save: {}", saved.message);
                if (!saved.message.empty()) ShowNotice(saved.message);
                return SaveResult::Refused;
            }
            if (g_blood) {
                WritingTools::Bleed();
            } else if (WritingTools::UseInk() == WritingTools::Ink::RanDry) {
                Notify(Strings::Get("$IQ_InkRanDry"));
            }
            for (std::size_t i = 0; i < bodies->size(); ++i) g_edit[i] = { (*bodies)[i], false };
            if (g_sessionBlank != 0) ReplaceBlank(saved.book);
            if (readText) *readText = std::move(saved.text);
            SKSE::log::info("[Editor] Saved");
            return SaveResult::Saved;
        }

        // ---- Edit mode ----

        void EnterEditMode()
        {
            auto* movie = BookMovie();
            if (!movie || !SendContent(movie)) {
                EndSession();
                return;
            }
            // False when the loaded book.swf isn't ours (another mod's won): no edit mode.
            if (!movie->Invoke("_root.BookMenu_mc.EnterEditMode", nullptr, nullptr, 0)) {
                SKSE::log::warn("[Editor] book.swf has no EnterEditMode: it isn't Ink & Quill's (check the load order)");
                EndSession();
                return;
            }
            const bool marked = !g_session.document.marked.empty();
            if (marked) {
                // The runs as loaded are what a save compares against.
                const auto bodies = ReadBodies(true);
                if (!bodies) {
                    movie->Invoke("_root.BookMenu_mc.ExitEditMode", nullptr, nullptr, 0);
                    EndSession();
                    return;
                }
                for (const auto& body : *bodies) g_edit.push_back({ body });
                RE::GFxValue check;
                movie->Invoke("_root.BookMenu_mc.EditCheckLayout", &check, nullptr, 0);
                SKSE::log::info("[Editor] Marked text: {} runs; layout {}", g_edit.size(),
                                check.IsString() ? check.GetString() : "unchecked");
            }
            g_active = true;
            if (g_blood) {
                RE::GFxValue on;
                on.SetBoolean(true);
                movie->Invoke("_root.BookMenu_mc.EditSetBlood", nullptr, &on, 1);
                SKSE::log::info("[Editor] Writing in blood");
            }
            SetTextInput(true);
            SKSE::log::info("[Editor] Edit mode on");
            if (marked && g_session.caretRun >= 0) {
                RE::GFxValue run;
                run.SetNumber(g_session.caretRun);
                movie->Invoke("_root.BookMenu_mc.EditFocusEntry", nullptr, &run, 1);
            } else if (!marked && (g_session.startNewEntry || g_edit.empty())) {
                // A document with no entries has nowhere to type: writing in it is a new entry.
                AppendEntry();
            }
        }

        void LeaveEditMode()
        {
            if (!g_active.exchange(false)) return;
            g_prompting = false;
            SetTextInput(false);
            SKSE::log::info("[Editor] Edit mode off");
            EndSession();
        }

        // Ask the book menu to close.  While writing, the close hook turns it into the save
        // prompt if there are unsaved changes.
        void RequestClose()
        {
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::BookMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }

        // Close without asking: edit mode ends first, so the close hook lets it through.
        void CloseBook()
        {
            LeaveEditMode();
            RequestClose();
        }

        // Saved, or nothing to save: false keeps writing (refused, or the text can't be read).
        bool SavedOrStay(std::string* readText = nullptr)
        {
            switch (Save(readText)) {
            case SaveResult::Nothing:
            case SaveResult::Saved:
                return true;
            case SaveResult::Unreadable:
                Notify(Strings::Get("$IQ_SaveFailed"));
                return false;
            case SaveResult::Refused:
                return false;
            }
            return false;
        }

        // The edit key while writing: save, and read again on the same spread.
        void SaveAndRead()
        {
            std::string text;
            const auto result = Save(&text);
            if (result == SaveResult::Unreadable) Notify(Strings::Get("$IQ_SaveFailed"));
            if (result == SaveResult::Unreadable || result == SaveResult::Refused) return;
            auto* movie = BookMovie();
            LeaveEditMode();
            // Saved without a reading text: the client closes the book.  Nothing saved: the SWF reads the text it had.
            if (result == SaveResult::Saved && text.empty()) {
                SKSE::log::info("[Editor] No text to read again: closing the book");
                RequestClose();
                return;
            }
            RE::GFxValue arg;
            arg.SetString(text.c_str());
            if (!movie || !movie->Invoke("_root.BookMenu_mc.ReturnToReading", nullptr, &arg, 1)) {
                SKSE::log::warn("[Editor] Couldn't return to reading: closing the book");
                RequestClose();
            }
        }

        // ---- Entries ----

        // The entry the caret is in (page order), if any.
        std::optional<std::size_t> CaretEntry()
        {
            auto* movie = BookMovie();
            RE::GFxValue result;
            if (!movie || !movie->Invoke("_root.BookMenu_mc.EditCurrentEntry", &result, nullptr, 0) ||
                !result.IsNumber() || result.GetNumber() < 0) {
                return std::nullopt;
            }
            const auto index = static_cast<std::size_t>(result.GetNumber());
            return index < g_edit.size() ? std::optional(index) : std::nullopt;
        }

        enum class Change { Type, EraseBack, EraseForward };

        // A key that changes the text needs the caret in an entry, and an erase something
        // erasable.  Ink and blood are charged on save.  False: the key does nothing.
        bool CanWrite(Change change = Change::Type)
        {
            if (!CaretEntry()) return false;
            if (change == Change::Type) return true;
            RE::GFxValue forward;
            forward.SetBoolean(change == Change::EraseForward);
            RE::GFxValue can;
            return BookMovie()->Invoke("_root.BookMenu_mc.EditCanErase", &can, &forward, 1) && can.IsBool() &&
                   can.GetBool();
        }

        // Save / Discard / Keep writing.
        class SavePromptCallback : public RE::IMessageBoxCallback {
        public:
            void Run(std::uint8_t a_button) override
            {
                EndPrompt();
                if (!g_active) return;
                if (a_button == 0) {
                    if (SavedOrStay()) CloseBook();
                } else if (a_button == 1) {
                    SKSE::log::info("[Editor] Changes discarded");
                    if (g_session.client.onDiscard) g_session.client.onDiscard();
                    CloseBook();
                }
            }
        };

        void ShowSavePrompt()
        {
            ShowPrompt(Strings::Get("$IQ_SavePrompt"), { "$IQ_Save", "$IQ_Discard", "$IQ_KeepWriting" }, 2,
                       new SavePromptCallback());
        }

        // Tear out entry i (page order) now, from the editor and through the client.  Other
        // entries' unsaved changes stay in the editor.
        void Remove(std::size_t i)
        {
            if (!g_session.document.marked.empty()) {
                // The client renders without it and reloads.
                SKSE::log::info("[Editor] Run {} to be removed by the client", i);
                if (g_session.client.onRemove) g_session.client.onRemove(i);
                return;
            }
            auto* movie = BookMovie();
            RE::GFxValue arg;
            arg.SetNumber(static_cast<double>(i));
            RE::GFxValue removed;
            if (i >= g_edit.size() || !movie || !movie->Invoke("_root.BookMenu_mc.EditRemoveEntry", &removed, &arg, 1) ||
                !removed.IsBool() || !removed.GetBool()) {
                SKSE::log::warn("[Editor] The SWF couldn't remove entry {}", i);
                return;
            }
            g_edit.erase(g_edit.begin() + static_cast<std::ptrdiff_t>(i));
            const std::string dates = g_session.client.onRemove ? g_session.client.onRemove(i) : std::string();
            RE::GFxValue datesArg;
            datesArg.SetString(dates.c_str());
            movie->Invoke("_root.BookMenu_mc.EditSetDates", nullptr, &datesArg, 1);
            SKSE::log::info("[Editor] Tore out entry {}", i);
        }

        class RemoveCallback : public RE::IMessageBoxCallback {
        public:
            explicit RemoveCallback(std::size_t a_entry) : entry_(a_entry) {}
            void Run(std::uint8_t a_button) override
            {
                EndPrompt();
                if (g_active && a_button == 0) Remove(entry_);
            }

        private:
            std::size_t entry_;
        };

        // The remove key while writing: ask about the entry under the caret.
        void ShowRemovePrompt()
        {
            const auto entry = CaretEntry();
            if (!entry || !g_session.client.removePrompt) return;
            const auto question = g_session.client.removePrompt(*entry);
            if (!question.empty()) ShowPrompt(question, { "$IQ_Remove", "$IQ_Keep" }, 1, new RemoveCallback(*entry));
        }

        // ---- Starting ----

        // Write in blood / Put the quill down: the player has a quill but no ink.
        class BloodCallback : public RE::IMessageBoxCallback {
        public:
            void Run(std::uint8_t a_button) override
            {
                EndPrompt();
                if (a_button != 0) {
                    EndSession();
                    return;
                }
                g_blood = true;
                EnterEditMode();
            }
        };

        // A quill, then ink or the blood prompt, then edit mode (docs/EDITOR.md#starting).
        void Start()
        {
            g_blood = false;
            if (auto* ui = RE::UI::GetSingleton(); !ui || !ui->GameIsPaused()) {
                // Key handling assumes the paused book menu's main-thread input (Skyrim Souls
                // RE, for one, can unpause it).
                SKSE::log::warn("[Editor] The book menu doesn't pause the game: no writing");
                Notify(Strings::Get("$IQ_NeedsPause"));
                EndSession();
                return;
            }
            if (!WritingTools::HasQuill()) {
                SKSE::log::info("[Editor] No quill: can't write");
                // In a blank a HUD notice, not a prompt: blanks get read often.
                if (g_sessionBlank != 0) {
                    Notify(Strings::Get("$IQ_NeedsQuill"));
                } else {
                    ShowNotice(Strings::Get("$IQ_NeedsQuill"));
                }
                EndSession();
                return;
            }
            if (!WritingTools::HasInk()) {
                SKSE::log::info("[Editor] No ink: offering blood");
                ShowPrompt(Strings::Get("$IQ_BloodPrompt"), { "$IQ_BloodYes", "$IQ_BloodNo" }, 1, new BloodCallback());
                return;
            }
            EnterEditMode();
        }

        // The edit key while reading: the first owner with a session for the open book.
        void BeginFromKey()
        {
            auto* book = RE::BookMenu::GetTargetForm();
            if (!book) return;
            if (IsOwnBlank(book)) {
                OpenBlank();
                return;
            }
            for (const auto& owner : g_owners) {
                if (owner(book)) return;
            }
            SKSE::log::info("[Editor] No client owns {:08X}: the edit key does nothing", book->GetFormID());
        }

        // ---- Menu and input ----

        class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                                  RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (a_event && a_event->menuName == RE::BookMenu::MENU_NAME && a_event->opening) {
                    SKSE::GetTaskInterface()->AddUITask([]() { NoteBlank(); });
                }
                if (a_event && a_event->menuName == RE::BookMenu::MENU_NAME && !a_event->opening) {
                    LeaveEditMode();
                    g_blankOnOpen = 0;
                    // A session waiting for this menu that never began.
                    if (std::exchange(g_beginOnOpen, 0) != 0) EndSession();
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // Every frame the book menu is open: once a book waiting to be written in has its text, begin.
        struct BookMenuAdvanceMovie {
            static void thunk(RE::IMenu* a_menu, float a_interval, std::uint32_t a_currentTime)
            {
                func(a_menu, a_interval, a_currentTime);
                const RE::FormID waiting = g_beginOnOpen ? g_beginOnOpen : g_blankOnOpen;
                if (waiting == 0 || g_active) return;
                auto* book = RE::BookMenu::GetTargetForm();
                auto* movie = BookMovie();
                if (!book || book->GetFormID() != waiting || !movie) return;
                RE::GFxValue ready;
                if (movie->Invoke("_root.BookMenu_mc.EditReady", &ready, nullptr, 0) && ready.IsBool() && ready.GetBool()) {
                    if (std::exchange(g_beginOnOpen, 0)) {
                        SKSE::GetTaskInterface()->AddUITask([]() { Start(); });
                    } else {
                        g_blankOnOpen = 0;
                        SKSE::GetTaskInterface()->AddUITask([]() { OpenBlank(); });
                    }
                }
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        // True if this press (or held repeat) should produce input now.
        bool ShouldProcess(const RE::ButtonEvent* button, std::uint32_t scanCode)
        {
            if (button->IsUp()) {
                if (scanCode == g_lastScanCode) {
                    g_lastScanCode = 0;
                    g_keyRepeating = false;
                }
                return false;
            }
            const auto now = std::chrono::steady_clock::now();
            if (button->IsDown()) {
                g_lastScanCode = scanCode;
                g_lastKeyTime = now;
                g_keyRepeating = false;
                return true;
            }
            if (!button->IsHeld() || scanCode != g_lastScanCode) return false;
            if (now - g_lastKeyTime < (g_keyRepeating ? kKeyRepeatRate : kKeyRepeatDelay)) return false;
            g_keyRepeating = true;
            g_lastKeyTime = now;
            return true;
        }

        void HandleKey(std::uint32_t scanCode)
        {
            if (scanCode == kEscape) {
                // One press closes the book (text input would swallow it); the close hook
                // asks first if there are unsaved changes.
                RequestClose();
                return;
            }
            static const std::unordered_map<std::uint32_t, const char*> kCursorKeys{
                { kLeft, "left" }, { kRight, "right" }, { kUp, "up" }, { kDown, "down" }, { kHome, "home" }, { kEnd, "end" },
            };
            if (const auto key = kCursorKeys.find(scanCode); key != kCursorKeys.end()) {
                Invoke("EditMoveCursor", key->second);
                return;
            }
            if (scanCode == kBackspace) {
                if (CanWrite(Change::EraseBack)) Invoke("EditBackspace");
                return;
            }
            if (scanCode == kDelete) {
                if (CanWrite(Change::EraseForward)) Invoke("EditDelete");
                return;
            }
            if (scanCode == kEnter) {
                if (CanWrite()) Invoke("AppendEditChar", "\n");
                return;
            }

            // The characters for this key with the active keyboard layout and modifiers (a dead
            // key that doesn't combine gives two).
            BYTE keyState[256] = {};
            GetKeyboardState(keyState);
            WCHAR chars[8] = {};
            const auto vk = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK);
            const int count = ToUnicode(vk, scanCode, keyState, chars, 8, 0);
            std::wstring typed;
            for (int i = 0; i < count; ++i) {
                if (chars[i] >= 32) typed += chars[i];
            }
            if (typed.empty()) return;
            char utf8[32] = {};
            if (WideCharToMultiByte(CP_UTF8, 0, typed.c_str(), static_cast<int>(typed.size()), utf8, sizeof(utf8) - 1,
                                    nullptr, nullptr) > 0 &&
                CanWrite()) {
                Invoke("AppendEditChar", utf8);
            }
        }

        // Every way of closing the book reaches the menu here: with unsaved changes it's held back
        // and the save prompt opens (docs/EDITOR.md#closing).
        struct BookMenuProcessMessage {
            // Hold back a close?  If not, edit mode ends now: the menu's second hide, after its close
            // animation, comes when the SWF has dropped the editor.
            static bool HoldClose()
            {
                if (g_prompting) return true;
                const auto changes = HasChanges();
                if (changes.value_or(false)) {
                    ShowSavePrompt();
                    return true;
                }
                if (!changes) SKSE::log::error("[Editor] Can't read the editor's text: closing without saving");
                LeaveEditMode();
                return false;
            }

            static RE::UI_MESSAGE_RESULTS thunk(RE::IMenu* a_menu, RE::UIMessage& a_message)
            {
                const auto type = a_message.type.get();
                if (g_active) {
                    const auto* data = type == RE::UI_MESSAGE_TYPE::kUserEvent
                                           ? static_cast<RE::BSUIMessageData*>(a_message.data)
                                           : nullptr;
                    // A gamepad's B: the menu would play its close animation before its hide.
                    if (data && data->fixedStr == "Cancel" && HoldClose()) return RE::UI_MESSAGE_RESULTS::kHandled;
                    if (type == RE::UI_MESSAGE_TYPE::kHide && HoldClose()) return RE::UI_MESSAGE_RESULTS::kIgnore;
                }
                return func(a_menu, a_message);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        class InputSink : public RE::BSTEventSink<RE::InputEvent*> {
        public:
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                                  RE::BSTEventSource<RE::InputEvent*>*) override
            {
                for (auto* event = a_event ? *a_event : nullptr; event; event = event->next) {
                    auto* button = event->AsButtonEvent();
                    if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;
                    const auto code = button->GetIDCode();
                    if (g_prompting) continue;  // a prompt's keys are its own

                    if (!g_active) {
                        // The edit key while a book is open: write in it, if a client owns it.  The
                        // menu doesn't get it.
                        auto* ui = RE::UI::GetSingleton();
                        if (code == Settings::EditKey() && button->IsDown() && ui && ui->IsMenuOpen(RE::BookMenu::MENU_NAME)) {
                            button->SetUserEvent("");
                            SKSE::GetTaskInterface()->AddUITask([]() { BeginFromKey(); });
                        }
                        continue;
                    }

                    // Writing (paused: the main thread).  The menu turns pages by key code, so the SWF
                    // refuses turns while keys are used; the blanked user event stops everything else.
                    // Clients' own sinks still see the key code (a client's new-entry key).
                    Invoke("EditSuppressTurn");
                    button->SetUserEvent("");
                    if (code == Settings::RemoveKey()) {
                        if (button->IsDown()) ShowRemovePrompt();
                        continue;
                    }
                    if (code == Settings::EditKey()) {
                        if (button->IsDown()) SaveAndRead();
                        continue;
                    }
                    if (std::ranges::find(kModifiers, code) != std::end(kModifiers)) continue;
                    if (ShouldProcess(button, code)) HandleKey(code);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void AddOwner(Owner owner) { g_owners.push_back(std::move(owner)); }

    void RegisterBlank(RE::FormID blank, Owner onOpen)
    {
        if (blank != 0 && onOpen) g_blanks[blank] = std::move(onOpen);
    }

    bool ReplaceOpenBlank(RE::FormID bookId, const std::string& readingText)
    {
        auto* blank = RE::BookMenu::GetTargetForm();
        auto* movie = BookMovie();
        if (g_active || !movie || !IsOwnBlank(blank) || !SwapBlank(blank->GetFormID(), bookId)) return false;
        // A session begun from here on is the book's, not the blank's.
        g_openingBlank = 0;
        RE::GFxValue text;
        text.SetString(readingText.c_str());
        movie->Invoke("_root.BookMenu_mc.ReplaceBookText", nullptr, &text, 1);
        return true;
    }

    namespace {
        // A new session may take over: writing is on and nothing else is writing or asking.  If not, its onEnd.
        bool CanBegin(Session& session)
        {
            if (WritingMode::IsOn() && !g_active && !g_prompting) {
                if (std::exchange(g_beginOnOpen, 0) != 0) EndSession();  // one still waiting for its book
                return true;
            }
            SKSE::log::info("[Editor] No session: {}", WritingMode::IsOn() ? "already writing" : "writing is off");
            if (session.client.onEnd) session.client.onEnd();
            return false;
        }
    }

    bool Begin(Session session)
    {
        if (!CanBegin(session)) return false;
        g_session = std::move(session);
        g_sessionBlank = g_openingBlank;
        Start();
        return true;
    }

    bool BeginOnOpen(RE::FormID book, Session session)
    {
        if (!CanBegin(session)) return false;
        g_session = std::move(session);
        g_beginOnOpen = book;
        return true;
    }

    std::optional<std::vector<std::string>> CurrentRuns()
    {
        if (!g_active || g_session.document.marked.empty()) return std::nullopt;
        return ReadBodies();
    }

    bool Reload(std::string marked, const std::vector<int>& from, int caretRun, int caretOffset)
    {
        auto* movie = BookMovie();
        if (!g_active || g_session.document.marked.empty() || marked.empty() || !movie) return false;
        const auto& doc = g_session.document;
        RE::GFxValue args[5];
        args[0].SetString(marked.c_str());
        args[1].SetString(doc.runFont.c_str());
        args[2].SetNumber(doc.runSize);
        args[3].SetNumber(caretRun);
        args[4].SetNumber(caretOffset);
        RE::GFxValue runs;
        if (!movie->Invoke("_root.BookMenu_mc.EditReload", &runs, args, 5) || !runs.IsNumber() || runs.GetNumber() < 0) {
            SKSE::log::error("[Editor] The SWF couldn't reload the text");
            return false;
        }
        // Each run keeps the text it was last saved with, so its unsaved changes still count.
        const auto count = static_cast<std::size_t>(runs.GetNumber());
        if (from.size() != count) SKSE::log::warn("[Editor] Reload: {} runs, but {} given where they came from", count, from.size());
        std::vector<Edited> edit(count);
        for (std::size_t i = 0; i < count && i < from.size(); ++i) {
            if (from[i] >= 0 && static_cast<std::size_t>(from[i]) < g_edit.size()) edit[i] = g_edit[from[i]];
        }
        g_edit = std::move(edit);
        g_session.document.marked = std::move(marked);
        SKSE::log::info("[Editor] Reloaded: {} runs", count);
        return true;
    }

    void AppendEntry()
    {
        auto* movie = BookMovie();
        if (!g_active || !movie || !g_session.client.newEntry) return;
        // One new entry at a time: another press goes back to the one not saved yet.
        for (std::size_t i = g_edit.size(); i-- > 0;) {
            if (g_edit[i].added) {
                RE::GFxValue arg;
                arg.SetNumber(static_cast<double>(i));
                movie->Invoke("_root.BookMenu_mc.EditFocusEntry", nullptr, &arg, 1);
                return;
            }
        }
        auto heading = g_session.client.newEntry();
        if (!heading) return;
        if (g_blood && !heading->empty()) *heading = std::format("{}{}{}", kBloodOpen, *heading, kBloodClose);
        RE::GFxValue arg;
        arg.SetString(heading->c_str());
        RE::GFxValue index;
        if (!movie->Invoke("_root.BookMenu_mc.EditAppendEntry", &index, &arg, 1) || !index.IsNumber() ||
            static_cast<std::size_t>(index.GetNumber()) != g_edit.size()) {
            SKSE::log::warn("[Editor] The SWF couldn't add a new entry");
            return;
        }
        g_edit.push_back({ std::string(), true });
        SKSE::log::info("[Editor] New entry {}", g_edit.size() - 1);
    }

    bool IsWriting() { return g_active; }

    bool InBlood() { return g_active && g_blood; }

    void Register()
    {
        static MenuSink menuSink;
        static InputSink inputSink;
        if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuSink);
        // First in line, ahead of MenuControls, so blanked keys never reach the menu.
        if (auto* input = RE::BSInputDeviceManager::GetSingleton()) input->PrependEventSink(&inputSink);
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BookMenu[0] };
        BookMenuProcessMessage::func = vtable.write_vfunc(0x4, BookMenuProcessMessage::thunk);
        BookMenuAdvanceMovie::func = vtable.write_vfunc(0x5, BookMenuAdvanceMovie::thunk);
        SKSE::log::info("[Editor] Registered");
    }

    void Reset()
    {
        g_active = false;
        g_prompting = false;
        g_beginOnOpen = 0;
        g_blankOnOpen = 0;
        g_blood = false;
        EndSession();
    }

}
