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

#include "Papyrus.h"

#include "Clients.h"
#include "Keys.h"
#include "Settings.h"

namespace InkAndQuill::Papyrus {

    namespace {
        constexpr auto kScript = "InkAndQuill_MCM";

        // An unknown name reads 0 (logged).
        const Settings::Setting* Find(const RE::BSFixedString& name)
        {
            const auto* s = Settings::Find(name.c_str());
            if (!s) SKSE::log::error("[Papyrus] Unknown setting '{}'", name.c_str());
            return s;
        }

        std::int32_t GetSetting(RE::StaticFunctionTag*, RE::BSFixedString name)
        {
            const auto* s = Find(name);
            return s ? Settings::Get(*s) : 0;
        }

        std::int32_t GetSettingDefault(RE::StaticFunctionTag*, RE::BSFixedString name)
        {
            const auto* s = Find(name);
            return s ? s->defaultValue : 0;
        }

        std::int32_t GetSettingMin(RE::StaticFunctionTag*, RE::BSFixedString name)
        {
            const auto* s = Find(name);
            return s ? s->min : 0;
        }

        std::int32_t GetSettingMax(RE::StaticFunctionTag*, RE::BSFixedString name)
        {
            const auto* s = Find(name);
            return s ? s->max : 0;
        }

        void SetSetting(RE::StaticFunctionTag*, RE::BSFixedString name, std::int32_t value)
        {
            if (const auto* s = Find(name)) Settings::Set(*s, value);
        }

        // Whether a key can be the edit key: 0 yes, 1 not a keyboard key, 2 a key that types or edits (Keys::Check).
        std::int32_t GetKeyProblem(RE::StaticFunctionTag*, std::int32_t code)
        {
            return static_cast<std::int32_t>(Keys::Check(static_cast<std::uint32_t>(code)));
        }

        std::int32_t GetClientCount(RE::StaticFunctionTag*) { return static_cast<std::int32_t>(Clients::All().size()); }

        // Its display name, or its DLL's file name without the extension.
        RE::BSFixedString GetClientName(RE::StaticFunctionTag*, std::int32_t index)
        {
            const auto all = Clients::All();
            if (index < 0 || static_cast<std::size_t>(index) >= all.size()) return "";
            const auto& client = all[index];
            if (!client.name.empty()) return client.name;
            const auto dot = client.file.rfind('.');
            return dot == std::string::npos ? client.file : client.file.substr(0, dot);
        }

        bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm)
        {
            vm->RegisterFunction("GetSetting", kScript, GetSetting);
            vm->RegisterFunction("SetSetting", kScript, SetSetting);
            vm->RegisterFunction("GetSettingDefault", kScript, GetSettingDefault);
            vm->RegisterFunction("GetSettingMin", kScript, GetSettingMin);
            vm->RegisterFunction("GetSettingMax", kScript, GetSettingMax);
            vm->RegisterFunction("GetKeyProblem", kScript, GetKeyProblem);
            vm->RegisterFunction("GetClientCount", kScript, GetClientCount);
            vm->RegisterFunction("GetClientName", kScript, GetClientName);
            return true;
        }
    }

    void Register()
    {
        if (auto* papyrus = SKSE::GetPapyrusInterface(); !papyrus || !papyrus->Register(RegisterFunctions)) {
            SKSE::log::error("[Papyrus] Registering the natives failed");
        }
    }

}
