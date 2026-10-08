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

// Work for SKSE's UI thread that catches what it throws (every task does), and HUD notices.
namespace InkAndQuill::Tasks {

    inline void QueueUI(std::function<void()> work, const char* who)
    {
        SKSE::GetTaskInterface()->AddUITask([work = std::move(work), who]() {
            try {
                work();
            } catch (const std::exception& e) {
                SKSE::log::error("[{}] A UI task failed: {}", who, e.what());
            } catch (...) {
                SKSE::log::error("[{}] A UI task failed", who);
            }
        });
    }

    inline void Notify(const std::string& text) { RE::SendHUDMessage::ShowHUDMessage(text.c_str()); }

}
