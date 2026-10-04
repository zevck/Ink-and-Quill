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

#include "BookMovie.h"
#include "Clipboard.h"
#include "Input.h"
#include "Keys.h"
#include "QuillCursor.h"
#include "Settings.h"
#include "Strings.h"
#include "Suggestions.h"
#include "WritingMode.h"
#include "WritingTools.h"

#include <Windows.h>

namespace InkAndQuill::Editor {

    namespace {
        using namespace Keys;

        std::atomic<bool> g_bookOpen{ false };  // the book menu is open (MenuSink), for the input thread

        // A UI task that can't throw past SKSE's queue (CLAUDE.md: every task catches).
        void QueueUI(std::function<void()> work)
        {
            SKSE::GetTaskInterface()->AddUITask([work = std::move(work)]() {
                try {
                    work();
                } catch (const std::exception& e) {
                    SKSE::log::error("[Editor] A UI task failed: {}", e.what());
                } catch (...) {
                    SKSE::log::error("[Editor] A UI task failed");
                }
            });
        }

        // ---- The session (the UI's thread: the input sink queues its work there) ----

        // A run in the editor: its text as loaded or last saved.
        struct Edited {
            std::string saved;
        };

        std::vector<Owner> g_owners;
        Session g_session;                       // the session writing, or waiting on the blood prompt
        std::vector<Edited> g_edit;              // page order, as the SWF's EditGetBodies
        std::vector<std::string> g_started;      // the runs as the session began: a Reload's from -2 - k is run k's
        std::atomic<bool> g_active{ false };     // edit mode is on in the open book menu
        std::atomic<bool> g_prompting{ false };  // a prompt is open over the book: keys belong to it
        bool g_blood = false;                    // this session is in blood (no ink)
        bool g_free = false;                     // this session needs no quill or ink (Settings::kRequireQuillAndInk off)
        RE::FormID g_beginOnOpen = 0;            // begin g_session once this book is open with its text

        // Blanks (docs/EDITOR.md#blanks).
        std::unordered_map<RE::FormID, Owner> g_blanks;
        RE::FormID g_blankOnOpen = 0;   // a blank read from the inventory: open it once its text is in
        RE::FormID g_openingBlank = 0;  // the blank whose onOpen is running: the session it begins is for it
        RE::FormID g_sessionBlank = 0;  // the blank the session writes in, until a save replaces it

        // The session is over: nothing of it is kept, and the client hears it once (onEnd), last.
        std::uint64_t g_sessionSerial = 0;  // bumped whenever a session ends: a client prompt answers only its own

        void EndSession()
        {
            ++g_sessionSerial;
            auto onEnd = std::move(g_session.client.onEnd);
            g_session = {};
            g_edit.clear();
            g_started.clear();
            g_sessionBlank = 0;
            if (onEnd) onEnd();
        }

        void Notify(const std::string& text) { RE::SendHUDMessage::ShowHUDMessage(text.c_str()); }

        RE::GFxMovieView* BookMovie() { return Book::Movie(); }
        void Invoke(const char* function, const char* argument = nullptr) { Book::Call(function, nullptr, argument); }

        // Keys reach the menu blanked (InputSink), so no controls need turning off; the mouse
        // keeps the book's own page turns (left click previous, right click next).
        void SetTextInput(bool enabled)
        {
            if (auto* controls = RE::ControlMap::GetSingleton()) controls->AllowTextInput(enabled);
        }

        // ---- Prompts over the book ----

        void ShowPromptText(const std::string& body, const std::vector<std::string>& buttons, std::int32_t cancelButton,
                            RE::IMessageBoxCallback* callback)
        {
            auto* data = RE::UIMessageDataFactory::Create<RE::MessageBoxData>();
            if (!data) return;
            data->bodyText = body.c_str();
            for (const auto& button : buttons) data->buttonText.push_back(button.c_str());
            data->cancelButtonIndex = cancelButton;  // Escape on the prompt
            data->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(callback);
            // The prompt's own keys (Enter, Escape) must reach it.
            Suggestions::Clear();
            g_prompting = true;
            SetTextInput(false);
            RE::MessageBoxMenu::QueueMessage(data);
        }

        // Ink & Quill's own prompts: the buttons are translation keys.
        void ShowPrompt(const std::string& body, std::initializer_list<std::string_view> buttons,
                        std::int32_t cancelButton, RE::IMessageBoxCallback* callback)
        {
            std::vector<std::string> texts;
            for (const auto button : buttons) texts.push_back(Strings::Get(button));
            ShowPromptText(body, texts, cancelButton, callback);
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

        // The session's marked text to the SWF; the runs are known once it has taken the markers out (EnterEditMode).
        bool SendContent(RE::GFxMovieView* movie)
        {
            g_edit.clear();
            const auto& doc = g_session.document;
            if (doc.marked.empty()) {
                SKSE::log::error("[Editor] A session without marked text: no writing");
                return false;
            }
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
            if (!IsOwnBlank(book)) return;
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
            if (!g_free && (g_blood ? !WritingTools::CanBleed() : !WritingTools::HasInk())) {
                // Checked when writing began; the inkwell or the health may have gone since.
                ShowNotice(Strings::Get(g_blood ? "$IQ_TooWeak" : "$IQ_NeedsInk"));
                return SaveResult::Refused;
            }
            auto saved = g_session.client.onSave ? g_session.client.onSave(*bodies) : Saved{};
            if (!saved.accepted) {
                SKSE::log::info("[Editor] The client refused the save: {}", saved.message);
                if (!saved.message.empty()) ShowNotice(saved.message);
                return SaveResult::Refused;
            }
            if (!g_free) {
                if (g_blood) {
                    WritingTools::Bleed();
                } else if (WritingTools::UseInk() == WritingTools::Ink::RanDry) {
                    Notify(Strings::Get("$IQ_InkRanDry"));
                }
            }
            for (std::size_t i = 0; i < bodies->size(); ++i) g_edit[i] = { (*bodies)[i] };
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
            // The runs as loaded are what a save compares against.
            const auto bodies = ReadBodies(true);
            if (!bodies) {
                movie->Invoke("_root.BookMenu_mc.ExitEditMode", nullptr, nullptr, 0);
                EndSession();
                return;
            }
            for (const auto& body : *bodies) g_edit.push_back({ body });
            g_started = *bodies;
            SKSE::log::info("[Editor] {} runs", g_edit.size());
            g_active = true;
            if (g_blood) {
                RE::GFxValue on;
                on.SetBoolean(true);
                movie->Invoke("_root.BookMenu_mc.EditSetBlood", nullptr, &on, 1);
                SKSE::log::info("[Editor] Writing in blood");
            }
            SetTextInput(true);
            QuillCursor::Show();
            SKSE::log::info("[Editor] Edit mode on");
            if (g_session.caretRun >= 0) {
                RE::GFxValue run;
                run.SetNumber(g_session.caretRun);
                movie->Invoke("_root.BookMenu_mc.EditFocusEntry", nullptr, &run, 1);
            }
        }

        void LeaveEditMode()
        {
            if (!g_active.exchange(false)) return;
            QuillCursor::Hide();
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

        // A key changed a run's text: the client hears it now, in the key's task, so a Reload shows with it.
        void Changed()
        {
            if (!g_active || g_prompting || !g_session.client.onChange) return;
            const auto run = CaretEntry();
            RE::GFxValue offset;
            if (!run || !Book::Call("EditCaretOffset", &offset) || !offset.IsNumber()) return;
            const auto onChange = g_session.client.onChange;  // a copy: the session may end in it
            onChange(static_cast<int>(*run), static_cast<int>(offset.GetNumber()));
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
            g_free = !Settings::Get(Settings::kRequireQuillAndInk);
            if (auto* ui = RE::UI::GetSingleton(); ui && !ui->GameIsPaused()) {
                SKSE::log::info("[Editor] The book menu doesn't pause the game (Skyrim Souls?)");
            }
            if (g_free) {
                EnterEditMode();
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
            if (!WritingTools::HasInk() && !Settings::Get(Settings::kBlood)) {
                SKSE::log::info("[Editor] No ink, and blood is off: can't write");
                if (g_sessionBlank != 0) {
                    Notify(Strings::Get("$IQ_NeedsInk"));
                } else {
                    ShowNotice(Strings::Get("$IQ_NeedsInk"));
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
            if (!g_bookOpen || g_active || g_prompting) return;  // closed, or begun, since the key was queued
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
                    g_bookOpen = true;
                    QueueUI([]() { NoteBlank(); });
                }
                if (a_event && a_event->menuName == RE::BookMenu::MENU_NAME && !a_event->opening) {
                    g_bookOpen = false;
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
                QuillCursor::Follow();
                const RE::FormID waiting = g_beginOnOpen ? g_beginOnOpen : g_blankOnOpen;
                if (waiting == 0 || g_active) return;
                auto* book = RE::BookMenu::GetTargetForm();
                auto* movie = BookMovie();
                if (!book || book->GetFormID() != waiting || !movie) return;
                RE::GFxValue ready;
                if (movie->Invoke("_root.BookMenu_mc.EditReady", &ready, nullptr, 0) && ready.IsBool() && ready.GetBool()) {
                    if (std::exchange(g_beginOnOpen, 0)) {
                        QueueUI([]() { Start(); });
                    } else {
                        g_blankOnOpen = 0;
                        QueueUI([]() { OpenBlank(); });
                    }
                }
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        void HandleKey(std::uint32_t scanCode)
        {
            if (QuillCursor::Adjust(scanCode)) return;
            if (std::string accepted; Suggestions::HandleKey(scanCode, accepted)) {
                if (!accepted.empty() && CanWrite()) {
                    Invoke("AppendEditChar", accepted.c_str());
                    Changed();
                }
                return;
            }
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
            if (scanCode == kBackspace || scanCode == kDelete) {
                const bool back = scanCode == kBackspace;
                if (CanWrite(back ? Change::EraseBack : Change::EraseForward)) {
                    Invoke(back ? "EditBackspace" : "EditDelete");
                    Changed();
                }
                return;
            }
            if (scanCode == kEnter) {
                if (CanWrite()) {
                    Invoke("AppendEditChar", "\n");
                    Changed();
                }
                return;
            }

            // The characters for this key with the active keyboard layout and modifiers (a dead
            // key that doesn't combine gives two).
            BYTE keyState[256] = {};
            GetKeyboardState(keyState);
            WCHAR chars[8] = {};
            const auto vk = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK);
            // Ctrl (not AltGr, which some layouts type with): Ctrl+V pastes, Ctrl+C copies the run the caret is in.
            if ((keyState[VK_CONTROL] & 0x80) && !(keyState[VK_MENU] & 0x80)) {
                if (vk == 'V' && CanWrite()) {
                    if (const auto text = Clipboard::ReadForTyping(); !text.empty()) {
                        Invoke("AppendEditChar", text.c_str());
                        Changed();
                    }
                } else if (vk == 'C') {
                    const auto run = CaretEntry();
                    const auto bodies = run ? ReadBodies() : std::nullopt;
                    if (bodies && Clipboard::Write((*bodies)[*run])) Notify(Strings::Get("$IQ_Copied"));
                }
                return;
            }
            const int count = ToUnicode(vk, scanCode, keyState, chars, 8, 0);
            std::wstring typed;
            for (int i = 0; i < count; ++i) {
                if (chars[i] >= 32) typed += chars[i];
            }
            if (typed.empty()) return;
            if (CanWrite()) {
                Invoke("AppendEditChar", Strings::Utf8(typed).c_str());
                Changed();
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
    }

    void AddOwner(Owner owner) { g_owners.push_back(std::move(owner)); }

    bool IsBookOpen() { return g_bookOpen; }

    void OnEditKey(bool writing)
    {
        if (!writing) {
            QueueUI([]() { BeginFromKey(); });
            return;
        }
        QueueUI([]() {
            if (g_active && !g_prompting) SaveAndRead();
        });
    }

    void OnKey(std::uint32_t scanCode)
    {
        QueueUI([scanCode]() {
            if (g_active && !g_prompting) HandleKey(scanCode);
        });
    }

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

    bool Reload(std::string marked, const std::string& readingText, const std::vector<int>& from, int caretRun,
                int caretOffset)
    {
        auto* movie = BookMovie();
        if (!g_active || g_session.document.marked.empty() || marked.empty() || !movie) return false;
        const auto& doc = g_session.document;
        RE::GFxValue args[6];
        args[0].SetString(marked.c_str());
        args[1].SetString(doc.runFont.c_str());
        args[2].SetNumber(doc.runSize);
        args[3].SetNumber(caretRun);
        args[4].SetNumber(caretOffset);
        args[5].SetString(readingText.c_str());
        RE::GFxValue runs;
        if (!movie->Invoke("_root.BookMenu_mc.EditReload", &runs, args, 6) || !runs.IsNumber() || runs.GetNumber() < 0) {
            SKSE::log::error("[Editor] The SWF couldn't reload the text");
            return false;
        }
        // Each run keeps the text it was last saved with, so its unsaved changes still count.
        const auto count = static_cast<std::size_t>(runs.GetNumber());
        if (from.size() != count) SKSE::log::warn("[Editor] Reload: {} runs, but {} given where they came from", count, from.size());
        std::vector<Edited> edit(count);
        for (std::size_t i = 0; i < count && i < from.size(); ++i) {
            if (from[i] >= 0 && static_cast<std::size_t>(from[i]) < g_edit.size()) edit[i] = g_edit[from[i]];
            const auto start = static_cast<std::size_t>(-2 - static_cast<long long>(from[i]));
            if (from[i] <= -2 && start < g_started.size()) edit[i].saved = g_started[start];
            if (from[i] <= -2 && start >= g_started.size()) {
                SKSE::log::warn("[Editor] Reload: run {} from start run {}, but the session began with {}: new", i, start, g_started.size());
            }
        }
        g_edit = std::move(edit);
        g_session.document.marked = std::move(marked);
        SKSE::log::info("[Editor] Reloaded: {} runs", count);
        return true;
    }

    namespace {
        // A client's prompt: its answer once the prompt is gone, only if the session that asked is still writing.
        class ClientPromptCallback : public RE::IMessageBoxCallback {
        public:
            ClientPromptCallback(std::function<void(int)> a_done, std::uint64_t a_serial) :
                done_(std::move(a_done)), serial_(a_serial) {}
            void Run(std::uint8_t a_button) override
            {
                EndPrompt();
                if (g_active && g_sessionSerial == serial_ && done_) done_(a_button);
            }

        private:
            std::function<void(int)> done_;
            std::uint64_t serial_;
        };
    }

    int CaretRun()
    {
        if (!g_active) return -1;
        const auto run = CaretEntry();
        return run ? static_cast<int>(*run) : -1;
    }

    bool Prompt(const std::string& text, const std::vector<std::string>& buttons, int cancelButton,
                std::function<void(int)> done)
    {
        if (!g_active || g_prompting || buttons.empty()) return false;
        ShowPromptText(text, buttons, cancelButton, new ClientPromptCallback(std::move(done), g_sessionSerial));
        return true;
    }

    bool IsWriting() { return g_active; }

    bool IsPrompting() { return g_prompting; }

    bool InBlood() { return g_active && g_blood; }

    bool WouldBeInBlood()
    {
        return Settings::Get(Settings::kRequireQuillAndInk) && Settings::Get(Settings::kBlood) && WritingTools::HasQuill() &&
               !WritingTools::HasInk();
    }

    void Register()
    {
        static MenuSink menuSink;
        if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuSink);
        Input::Register();
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BookMenu[0] };
        BookMenuProcessMessage::func = vtable.write_vfunc(0x4, BookMenuProcessMessage::thunk);
        BookMenuAdvanceMovie::func = vtable.write_vfunc(0x5, BookMenuAdvanceMovie::thunk);
        SKSE::log::info("[Editor] Registered");
    }

    void Reset()
    {
        QuillCursor::Hide();
        g_active = false;
        g_prompting = false;
        g_beginOnOpen = 0;
        g_blankOnOpen = 0;
        g_blood = false;
        EndSession();
    }

}
