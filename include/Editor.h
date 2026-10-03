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

#pragma once

// The player writing in the open book: book.swf's editor, keys, the prompts, ink and blood.
// Clients give the text and keep it.  Main thread only (the book menu pauses).  docs/EDITOR.md
namespace InkAndQuill::Editor {

    // Text in blood is between these (UTF-8 of U+E000 and U+E001), in headings and bodies.
    inline constexpr std::string_view kBloodOpen = "\xEE\x80\x80";
    inline constexpr std::string_view kBloodClose = "\xEE\x80\x81";
    // In marked text, what the player can't change is between these (U+E002 and U+E003).
    inline constexpr std::string_view kLockOpen = "\xEE\x80\x82";
    inline constexpr std::string_view kLockClose = "\xEE\x80\x83";

    // Marked text (docs/EDITOR.md#marked-text): the book's text as reading shows it, locks marked.  The runs between
    // locks are what the player writes, in order.
    struct Document {
        std::string marked;
        std::string runFont;  // optional: the format typed text takes, paragraph breaks at the page's size
        int runSize = 0;
    };

    // The client's answer to a save.  Refused: `message` is shown and writing goes on, nothing charged.
    // Accepted: ink or blood is charged; `text` is the book's text to read again (empty: the book closes).
    struct Saved {
        bool accepted = false;
        std::string message;
        std::string text;
        RE::FormID book = 0;  // a blank's session: the book that replaces the blank (0: the blank stays)
    };

    struct Client {
        std::function<Saved(const std::vector<std::string>& bodies)> onSave;  // the bodies in page order
        std::function<void()> onDiscard;
        // The session is over (saved and closed, discarded, refused to start, a load): once, always last.
        std::function<void()> onEnd;
        // The player changed a run's text: its index and the caret's offset in it (docs/API.md#reacting-to-typing).
        std::function<void(int run, int caretOffset)> onChange;
    };

    struct Session {
        Document document;
        Client client;
        int caretRun = -1;  // begin at the end of this run (-1: the page being read)
    };

    // The edit key in an open book asks each owner in turn; one that owns the book calls Begin and returns true.
    using Owner = std::function<bool(RE::TESObjectBOOK* book)>;
    void AddOwner(Owner owner);

    // A blank (docs/EDITOR.md#blanks): read from the player's inventory, or the edit key on it there, calls onOpen.
    // The client replaces it now (ReplaceOpenBlank), or calls Begin and answers the first save with Saved::book.
    void RegisterBlank(RE::FormID blank, Owner onOpen);

    // A blank open from the inventory, not being written in (in onOpen, typically): replace it now.  One blank goes,
    // the menu shows the book with readingText from its first page.  False: no blank open, or no such book.
    bool ReplaceOpenBlank(RE::FormID book, const std::string& readingText);

    // The open book: check the quill and ink (or offer blood), then edit mode.  Book menu open.  False: not started
    // now (writing off, already writing, a prompt open); the session's onEnd has run.
    bool Begin(Session session);

    // While writing: the runs as the player has them now, unsaved text included.
    std::optional<std::vector<std::string>> CurrentRuns();

    // While writing: the client's text rendered again (a new entry, a tear-out).  from[i]: which
    // run before the reload new run i was (-1: a new one), so unsaved changes are still known as such.
    // The caret goes to caretRun at caretOffset (-1: its end).  False: not writing.
    // readingText: the book's text as it now reads, shown if the edit key goes back to reading with nothing to save.
    bool Reload(std::string marked, const std::string& readingText, const std::vector<int>& from, int caretRun,
                int caretOffset);

    // Begin once this book's menu is open and has its text (its SetBookText comes after the menu opens).
    // The client opens the menu.  False as Begin.
    bool BeginOnOpen(RE::FormID book, Session session);

    // The player is writing, and in blood.
    bool IsWriting();
    bool IsPrompting();  // a prompt is open over the book
    bool InBlood();

    // A session begun now would be in blood, unless the player declines (quill and ink required, blood on, no ink):
    // for a client choosing a new entry's heading before it begins.
    bool WouldBeInBlood();

    // While writing: the run the caret is in, or -1.
    int CaretRun();

    // While writing: a client's message box over the book, keys its own until it closes.  done(button) once it has,
    // if the same session is still writing (never after its onEnd).  False: not writing, or a prompt is open.
    bool Prompt(const std::string& text, const std::vector<std::string>& buttons, int cancelButton,
                std::function<void(int)> done);

    // While writing, every keyboard event is taken from the game but clients' own keys (a new entry): this client's
    // set, replacing its last.  Keys that type or edit, and the edit key, are refused.  Returns how many were kept.
    int SetClientKeys(const void* client, const std::vector<std::uint32_t>& codes);

    // Once, at kDataLoaded: the book-menu and keyboard sinks and the menu hooks.
    void Register();

    // At the first kPostLoadGame or kNewGame (after Wheeler hooks the same call at kDataLoaded): the input dispatch hook.
    void InstallInputHook();

    // A load or new game: drop the session.
    void Reset();

}
