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

#include "LayoutTest.h"

#include "Editor.h"
#include "Settings.h"

#include <regex>

namespace InkAndQuill::LayoutTest {

    namespace {
        constexpr std::string_view kBreak = "[pagebreak]";

        std::string Locked(std::string_view text)
        {
            return std::format("{}{}{}", Editor::kLockOpen, text, Editor::kLockClose);
        }

        // A Physical Diaries journal (it starts with a page break: the blank first page): everything locked but each
        // entry's text, from after its heading's "\n\n" to its last paragraph's "</font>".  Headings on only.  The
        // first entry's text gives the hint (its first paragraph's font and size).
        std::string MarkJournal(const std::string& html, Editor::Document& doc)
        {
            static const std::regex kParagraph{ "<font face='([^']*)' size='([0-9]+)'>[^<]" };
            std::string out(Editor::kLockOpen);
            std::size_t page = 0;
            for (std::size_t start = 0;; ++page) {
                const auto end = html.find(kBreak, start);
                const std::string_view text = std::string_view(html).substr(start, end == std::string::npos ? html.npos : end - start);
                if (page >= 2) {
                    // Each page starts with the page break's own "\n\n"; the heading ends at the next one.
                    const auto first = text.find_first_not_of('\n');
                    auto head = first == std::string_view::npos ? std::string_view::npos : text.find("\n\n", first);
                    head = head == std::string_view::npos ? text.size() : head + 2;
                    std::match_results<std::string_view::const_iterator> match;
                    if (doc.runSize == 0 && std::regex_search(text.begin() + head, text.end(), match, kParagraph)) {
                        doc.runFont = match[1];
                        doc.runSize = std::stoi(match[2]);
                    }
                    auto body = text.rfind("</font>");
                    body = body == std::string_view::npos || body + 7 < head ? head : body + 7;
                    out += std::format("{}{}{}{}{}", text.substr(0, head), Editor::kLockClose, text.substr(head, body - head),
                                       Editor::kLockOpen, text.substr(body));
                } else {
                    out += text;
                }
                if (end == std::string::npos) break;
                out += kBreak;
                start = end + kBreak.size();
            }
            return out + std::string(Editor::kLockClose);
        }

        // Any other book: only its page breaks locked.
        std::string MarkBreaks(const std::string& html)
        {
            std::string out;
            std::size_t start = 0;
            for (auto at = html.find(kBreak); at != std::string::npos; at = html.find(kBreak, start)) {
                out += html.substr(start, at - start) + Locked(kBreak);
                start = at + kBreak.size();
            }
            return out + html.substr(start);
        }

        bool Owner(RE::TESObjectBOOK* book)
        {
            // No parent, as the book menu asks: Physical Diaries' hook gives other callers its text without font tags.
            RE::BSString text;
            book->GetDescription(text, nullptr);
            const std::string html = text.c_str() ? text.c_str() : "";
            if (html.empty()) return false;
            const bool journal = html.starts_with(kBreak);
            Editor::Session session;
            session.document.marked = journal ? MarkJournal(html, session.document) : MarkBreaks(html);
            session.client.onSave = [](const std::vector<std::string>& bodies) {
                for (std::size_t i = 0; i < bodies.size(); ++i) {
                    SKSE::log::info("[LayoutTest] Run {} ({} bytes): {}", i, bodies[i].size(), bodies[i].substr(0, 200));
                }
                // Development only: not translated.
                return Editor::Saved{ .message = std::format("Layout test: {} runs written to InkAndQuill.log. Nothing was kept.", bodies.size()) };
            };
            SKSE::log::info("[LayoutTest] {:08X}: {} text, font hint '{}' {}", book->GetFormID(), journal ? "journal" : "other",
                            session.document.runFont, session.document.runSize);
            Editor::Begin(std::move(session));
            return true;
        }
    }

    void Register()
    {
        if (!Settings::LayoutTest()) return;
        Editor::AddOwner(Owner);
        SKSE::log::warn("[LayoutTest] On: every book can be written in, and nothing is kept");
    }

}
