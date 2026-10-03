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

    // One entry: a heading the player can't change (may be empty) and its text.
    struct Entry {
        std::string heading;
        std::string body;
    };

    struct Document {
        std::string font = "$HandwrittenFont";
        int titleSize = 18, smallSize = 12, dateSize = 16, contentSize = 14;
        std::string title;  // the title page
        std::string dates;  // under the title
        std::vector<Entry> entries;
        // Marked text, used instead of the above when set (docs/EDITOR.md#marked-text): the book's text as reading
        // shows it, locks marked.  The runs between locks are the bodies, in order.
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
    };

    struct Client {
        std::function<Saved(const std::vector<std::string>& bodies)> onSave;  // the bodies in page order
        std::function<void()> onDiscard;
        // A new entry's heading, or nullopt if there's no room (the client says why).  Unset: no new entries.
        std::function<std::optional<std::string>()> newEntry;
        // The remove key on entry (run) i: the prompt's question; empty: nothing to remove there.  Unset: no removing.
        std::function<std::string(std::size_t)> removePrompt;
        // Entry i is to go (now, not on save).  Entries: the title page's dates after it.  Marked text: the client
        // removes it itself (CurrentRuns, render, Reload); the result is unused.
        std::function<std::string(std::size_t)> onRemove;
        // The session is over (saved and closed, discarded, refused to start, a load): once, always last.
        std::function<void()> onEnd;
    };

    struct Session {
        Document document;
        Client client;
        bool startNewEntry = false;  // entries: begin in a new entry
        int caretRun = -1;           // marked text: begin at the end of this run (-1: the page being read)
    };

    // The edit key in an open book asks each owner in turn; one that owns the book calls Begin and returns true.
    using Owner = std::function<bool(RE::TESObjectBOOK* book)>;
    void AddOwner(Owner owner);

    // The open book: check the quill and ink (or offer blood), then edit mode.  Book menu open.  False: not started
    // now (writing off, already writing, a prompt open); the session's onEnd has run.
    bool Begin(Session session);

    // Marked text, while writing: the runs as the player has them now, unsaved text included.
    std::optional<std::vector<std::string>> CurrentRuns();

    // Marked text, while writing: the client's text rendered again (a new entry, a tear-out).  from[i]: which
    // run before the reload new run i was (-1: a new one), so unsaved changes are still known as such.
    // The caret goes to caretRun at caretOffset (-1: its end).  False: not writing marked text.
    bool Reload(std::string marked, const std::vector<int>& from, int caretRun, int caretOffset);

    // Begin once this book's menu is open and has its text (its SetBookText comes after the menu opens).
    // The client opens the menu.  False as Begin.
    bool BeginOnOpen(RE::FormID book, Session session);

    // While writing: a new entry at the end, caret in it (a client's own key).
    void AppendEntry();

    // The player is writing, and in blood.
    bool IsWriting();
    bool InBlood();

    // Once, at kDataLoaded: the book-menu and keyboard sinks and the menu hooks.
    void Register();

    // A load or new game: drop the session.
    void Reset();

}
