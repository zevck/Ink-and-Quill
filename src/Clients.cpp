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

#include "Clients.h"

#include "Strings.h"

#include <Windows.h>

namespace InkAndQuill::Clients {

    namespace {
        struct Entry {
            HMODULE module;
            Client client;
        };

        std::mutex g_lock;
        std::vector<Entry> g_clients;

        HMODULE ModuleOf(const void* address)
        {
            HMODULE module = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCWSTR>(address), &module);
            return module;
        }

        // The entry for the module holding this address, added if new; nullptr for none or our own.
        Entry* Find(const void* caller)
        {
            const auto module = ModuleOf(caller);
            if (!module || module == ModuleOf(reinterpret_cast<const void*>(&Note))) return nullptr;
            for (auto& entry : g_clients) {
                if (entry.module == module) return &entry;
            }
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(module, path, MAX_PATH);
            std::wstring_view file = path;
            if (const auto slash = file.find_last_of(L"\\/"); slash != std::wstring_view::npos) file.remove_prefix(slash + 1);
            auto& entry = g_clients.emplace_back(Entry{ module, { Strings::Utf8(file), {} } });
            SKSE::log::info("[Clients] {} uses Ink & Quill", entry.client.file);
            return &entry;
        }
    }

    const void* Note(const void* caller)
    {
        try {
            std::scoped_lock lock(g_lock);
            auto* entry = Find(caller);
            return entry ? entry->module : nullptr;
        } catch (const std::exception& e) {
            SKSE::log::error("[Clients] Couldn't list a client: {}", e.what());
            return nullptr;
        }
    }

    void Name(const void* caller, std::string name)
    {
        try {
            std::scoped_lock lock(g_lock);
            if (auto* entry = Find(caller)) {
                SKSE::log::info("[Clients] {} is \"{}\"", entry->client.file, name);
                entry->client.name = std::move(name);
            }
        } catch (const std::exception& e) {
            SKSE::log::error("[Clients] Couldn't name a client: {}", e.what());
        }
    }

    std::vector<Client> All()
    {
        std::scoped_lock lock(g_lock);
        std::vector<Client> all;
        for (const auto& entry : g_clients) all.push_back(entry.client);
        return all;
    }

}
