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

// The mods using Ink & Quill, for the MCM: each DLL that calls the C API, found by the address it called from.
// See docs/API.md#clients.
namespace InkAndQuill::Clients {

    struct Client {
        std::string file;  // the DLL's file name, UTF-8
        std::string name;  // SetClientName's, or empty
    };

    // The DLL holding this code address (the API's caller): listed from now on.  Its id (the module), or nullptr.
    const void* Note(const void* caller);

    // Its display name.
    void Name(const void* caller, std::string name);

    // The caller's display name (else its DLL's file name), for the log.
    std::string NameOf(const void* caller);

    // In the order they first called.
    std::vector<Client> All();

}
