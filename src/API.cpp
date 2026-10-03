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

// The C API (include/InkAndQuillAPI.h) over Editor and WritingTools.  See docs/API.md.

#include "InkAndQuillAPI.h"

#include "Clients.h"
#include "Editor.h"
#include "WritingMode.h"
#include "WritingTools.h"

struct IQ_SaveReply {
    InkAndQuill::Editor::Saved saved;
    bool answered = false;
};


namespace {
    using namespace InkAndQuill;

    std::string Copy(const char* text) { return text ? text : ""; }

    // The C session as the editor's.  nullopt (logged, its onEnd run) if it can't be one.
    std::optional<Editor::Session> ToSession(const IQ_Session* in)
    {
        if (!in || in->size < sizeof(IQ_Session)) {
            SKSE::log::error("[API] A session from an older or broken client (size {}): ignored", in ? in->size : 0);
            return std::nullopt;
        }
        if (!in->markedText || !*in->markedText || !in->onSave) {
            SKSE::log::error("[API] A session without marked text or onSave: ignored");
            if (in->onEnd) in->onEnd(in->user);
            return std::nullopt;
        }
        const IQ_Session c = *in;
        Editor::Session session;
        session.document.marked = c.markedText;
        session.document.runFont = Copy(c.runFont);
        session.document.runSize = c.runSize;
        session.caretRun = c.caretRun;
        auto& client = session.client;
        client.onSave = [c](const std::vector<std::string>& runs) {
            std::vector<const char*> texts;
            texts.reserve(runs.size());
            for (const auto& run : runs) texts.push_back(run.c_str());
            IQ_SaveReply reply;
            c.onSave(c.user, texts.data(), static_cast<std::int32_t>(texts.size()), &reply);
            if (!reply.answered) SKSE::log::warn("[API] The client didn't answer the save: refused");
            return reply.saved;
        };
        if (c.onDiscard) client.onDiscard = [c]() { c.onDiscard(c.user); };
        if (c.onEnd) client.onEnd = [c]() { c.onEnd(c.user); };
        return session;
    }

    bool IsWritingOn() { return WritingMode::IsOn(); }
    bool IsWriting() { return Editor::IsWriting(); }
    bool InBlood() { return Editor::InBlood(); }
    bool WouldBeInBlood() { return Editor::WouldBeInBlood(); }
    std::int32_t RegisterKeys(const std::uint32_t* codes, std::int32_t count)
    {
        try {
            const void* client = Clients::Note(_ReturnAddress());
            std::vector<std::uint32_t> keys;
            if (codes && count > 0) keys.assign(codes, codes + count);
            return Editor::SetClientKeys(client, keys);
        } catch (const std::exception& e) {
            SKSE::log::error("[API] RegisterKeys: {}", e.what());
            return 0;
        }
    }
    std::int32_t CaretRun() { return Editor::CaretRun(); }

    bool Prompt(const char* text, const char* const* buttons, std::int32_t count, std::int32_t cancelButton,
                IQ_PromptDone done, void* user)
    {
        try {
            std::vector<std::string> labels;
            for (std::int32_t i = 0; buttons && i < count; ++i) labels.push_back(Copy(buttons[i]));
            std::function<void(int)> answer;
            if (done) answer = [done, user](int button) { done(user, button); };
            return Editor::Prompt(Copy(text), labels, cancelButton, std::move(answer));
        } catch (const std::exception& e) {
            SKSE::log::error("[API] Prompt: {}", e.what());
            return false;
        }
    }

    bool AddOwner(IQ_Owner owner, void* user)
    {
        Clients::Note(_ReturnAddress());
        if (!owner) return false;
        Editor::AddOwner([owner, user](RE::TESObjectBOOK* book) { return owner(user, book->GetFormID()); });
        return true;
    }

    bool BeginSession(const IQ_Session* in)
    {
        try {
            auto session = ToSession(in);
            return session && Editor::Begin(std::move(*session));
        } catch (const std::exception& e) {
            SKSE::log::error("[API] BeginSession: {}", e.what());
            return false;
        }
    }

    bool BeginSessionOnOpen(std::uint32_t book, const IQ_Session* in)
    {
        try {
            auto session = ToSession(in);
            return session && Editor::BeginOnOpen(book, std::move(*session));
        } catch (const std::exception& e) {
            SKSE::log::error("[API] BeginSessionOnOpen: {}", e.what());
            return false;
        }
    }

    std::int32_t CurrentRuns(IQ_RunVisitor visit, void* user)
    {
        const auto runs = Editor::CurrentRuns();
        if (!runs) return -1;
        for (std::size_t i = 0; visit && i < runs->size(); ++i) visit(user, static_cast<std::int32_t>(i), (*runs)[i].c_str());
        return static_cast<std::int32_t>(runs->size());
    }

    bool Reload(const char* marked, const char* readingText, const std::int32_t* from, std::int32_t count, std::int32_t caretRun,
                std::int32_t caretOffset)
    {
        try {
            std::vector<int> origins;
            if (from && count > 0) origins.assign(from, from + count);
            return Editor::Reload(Copy(marked), Copy(readingText), origins, caretRun, caretOffset);
        } catch (const std::exception& e) {
            SKSE::log::error("[API] Reload: {}", e.what());
            return false;
        }
    }

    void ReplySave(IQ_SaveReply* reply, bool accepted, const char* message, const char* readingText)
    {
        if (!reply) return;
        reply->saved = { accepted, Copy(message), Copy(readingText), 0 };
        reply->answered = true;
    }

    bool HasQuill() { return WritingTools::HasQuill(); }
    bool HasInk() { return WritingTools::HasInk(); }

    IQ_Ink UseInk()
    {
        switch (WritingTools::UseInk()) {
        case WritingTools::Ink::Used:
            return IQ_INK_USED;
        case WritingTools::Ink::RanDry:
            return IQ_INK_RAN_DRY;
        default:
            return IQ_INK_NONE;
        }
    }

    bool RegisterBlank(std::uint32_t blank, IQ_Owner onOpen, void* user)
    {
        Clients::Note(_ReturnAddress());
        if (!onOpen || blank == 0) return false;
        Editor::RegisterBlank(blank, [onOpen, user](RE::TESObjectBOOK* book) { return onOpen(user, book->GetFormID()); });
        return true;
    }

    void ReplySaveAsBook(IQ_SaveReply* reply, std::uint32_t book, const char* readingText)
    {
        if (!reply) return;
        reply->saved = { true, std::string(), Copy(readingText), book };
        reply->answered = true;
    }

    bool ReplaceBlank(std::uint32_t book, const char* readingText) { return Editor::ReplaceOpenBlank(book, Copy(readingText)); }

    void SetClientName(const char* name) { Clients::Name(_ReturnAddress(), Copy(name)); }

    bool CanBleed() { return WritingTools::CanBleed(); }
    bool Bleed() { return WritingTools::Bleed(); }

    constexpr IQ_API kAPI{
        .size = sizeof(IQ_API),
        .version = IQ_API_VERSION,
        .IsWritingOn = IsWritingOn,
        .IsWriting = IsWriting,
        .InBlood = InBlood,
        .AddOwner = AddOwner,
        .BeginSession = BeginSession,
        .BeginSessionOnOpen = BeginSessionOnOpen,
        .CurrentRuns = CurrentRuns,
        .Reload = Reload,
        .ReplySave = ReplySave,
        .HasQuill = HasQuill,
        .HasInk = HasInk,
        .UseInk = UseInk,
        .CanBleed = CanBleed,
        .Bleed = Bleed,
        .RegisterBlank = RegisterBlank,
        .ReplySaveAsBook = ReplySaveAsBook,
        .ReplaceBlank = ReplaceBlank,
        .SetClientName = SetClientName,
        .WouldBeInBlood = WouldBeInBlood,
        .RegisterKeys = RegisterKeys,
        .CaretRun = CaretRun,
        .Prompt = Prompt,
    };
}

extern "C" __declspec(dllexport) const IQ_API* IQ_GetAPI(std::uint32_t version)
{
    InkAndQuill::Clients::Note(_ReturnAddress());
    if (version > IQ_API_VERSION) {
        SKSE::log::warn("[API] A client wants API version {}; this is {}", version, IQ_API_VERSION);
        return nullptr;
    }
    return &kAPI;
}
