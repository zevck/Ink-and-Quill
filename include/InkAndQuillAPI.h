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
 *
 * For client mods: copy this header into your project.  It is plain C, so any compiler and CRT work.
 */

/*
 * Ink & Quill's C API.  docs/API.md has the full contract; in short:
 *
 *  - Get it at kPostLoad or later: GetModuleHandleA("InkAndQuill.dll"), GetProcAddress(module, "IQ_GetAPI"), then
 *    IQ_GetAPI(IQ_API_VERSION).  NULL: Ink & Quill is older than this header.
 *  - Every call is on the game's main thread (where SKSE runs UI tasks and where the paused book menu's input
 *    arrives), and every callback comes there.
 *  - Strings are UTF-8.  Ink & Quill copies every string it is given before the call returns; strings it passes
 *    to a callback are valid until the callback returns.
 *  - Text is "marked text": the book's text as the client renders it for reading, with what the player can't
 *    change between U+E002 and U+E003.  The runs between locks are what the player writes; blood text in a run
 *    is between U+E000 and U+E001.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IQ_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/* Answers given from inside a callback, through IQ_API::ReplySave and IQ_API::ReplyText. */
typedef struct IQ_SaveReply IQ_SaveReply;
typedef struct IQ_TextReply IQ_TextReply;

typedef enum IQ_Ink
{
    IQ_INK_NONE = 0,     /* no inkwell */
    IQ_INK_USED = 1,     /* one use taken */
    IQ_INK_RAN_DRY = 2,  /* that was the inkwell's last use: it's gone */
} IQ_Ink;

/* One book open for writing.  Ink & Quill copies it; user is handed back to every callback. */
typedef struct IQ_Session
{
    uint32_t size;           /* sizeof(IQ_Session) */
    const char* markedText;  /* required */
    const char* runFont;     /* optional: typed text's font; with runSize, paragraph breaks at the page's size */
    int32_t runSize;         /* optional: 0 for none */
    int32_t caretRun;        /* the run the caret starts at the end of; -1: the page being read */
    void* user;

    /* Required.  The runs in order.  Answer with ReplySave: accepted (ink or blood is charged; readingText is
       the book's text to read again, NULL or "" closes the book) or refused (message shown, nothing charged).
       No answer is a refusal. */
    void (*onSave)(void* user, const char* const* runs, int32_t count, IQ_SaveReply* reply);
    /* Optional.  The player discarded their changes. */
    void (*onDiscard)(void* user);
    /* Optional.  The remove key on a run: answer with ReplyText (the question to ask); no answer: nothing to
       remove there.  NULL: the remove key does nothing. */
    void (*removePrompt)(void* user, int32_t run, IQ_TextReply* reply);
    /* Optional.  The player confirmed: remove it now (CurrentRuns, render without it, Reload). */
    void (*onRemove)(void* user, int32_t run);
    /* Optional.  The session is over (closed, discarded, never started, a load): once, always last. */
    void (*onEnd)(void* user);
} IQ_Session;

/* The edit key in an open book no session is writing in.  Return true if this book is yours and you called
   BeginSession for it; false passes it to the next owner. */
typedef bool (*IQ_Owner)(void* user, uint32_t bookFormId);

typedef void (*IQ_RunVisitor)(void* user, int32_t index, const char* text);

typedef struct IQ_API
{
    uint32_t size;     /* sizeof(IQ_API) in the DLL: newer versions only add at the end */
    uint32_t version;  /* the DLL's IQ_API_VERSION */

    /* Writing is on (Ink & Quill's book.swf is the one the game loads). */
    bool (*IsWritingOn)(void);
    /* The player is writing now, and in blood (decided when writing starts, so a new heading can be red). */
    bool (*IsWriting)(void);
    bool (*InBlood)(void);

    /* Owners are asked in the order they were added.  Add at kDataLoaded or later. */
    bool (*AddOwner)(IQ_Owner owner, void* user);

    /* The open book: checks the quill and ink (or offers blood), then writing.  False: not started (writing off,
       already writing, a prompt open, an invalid session); onEnd has run. */
    bool (*BeginSession)(const IQ_Session* session);
    /* As BeginSession, once bookFormId's book menu is open and has its text.  The client opens the menu. */
    bool (*BeginSessionOnOpen)(uint32_t bookFormId, const IQ_Session* session);

    /* While writing: the runs as the player has them, unsaved text included.  Returns the count, or -1. */
    int32_t (*CurrentRuns)(IQ_RunVisitor visit, void* user);
    /* While writing: the text rendered again.  from[i] is the run new run i was before (-1: new), count the new
       run count; the caret goes to caretRun at caretOffset (-1: its end). */
    bool (*Reload)(const char* markedText, const int32_t* from, int32_t count, int32_t caretRun, int32_t caretOffset);

    void (*ReplySave)(IQ_SaveReply* reply, bool accepted, const char* message, const char* readingText);
    void (*ReplyText)(IQ_TextReply* reply, const char* text);

    /* Writing materials, for a client with its own writing UI. */
    bool (*HasQuill)(void);
    bool (*HasInk)(void);
    IQ_Ink (*UseInk)(void);
    bool (*CanBleed)(void);
    bool (*Bleed)(void);
} IQ_API;

/* Exported by InkAndQuill.dll as "IQ_GetAPI". */
typedef const IQ_API* (*IQ_GetAPI_t)(uint32_t version);

#ifdef __cplusplus
}
#endif
