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

#include "Serialization.h"

#include "Gemination.h"

namespace InkAndQuill::Serialization {

    namespace {
        constexpr std::uint32_t kSerializationId = 'INKQ';
        constexpr std::uint32_t kGeminationRecord = 'QOGM';

        void SaveCallback(SKSE::SerializationInterface* a_intfc)
        {
            try {
                Gemination::Save(a_intfc, kGeminationRecord);
            } catch (const std::exception& e) {
                SKSE::log::error("[Serialization] Saving failed: {}", e.what());
            }
        }

        // Every load and new game, before the save's records: nothing of the last game is kept.
        void RevertCallback(SKSE::SerializationInterface*) { Gemination::Revert(); }

        void LoadCallback(SKSE::SerializationInterface* a_intfc)
        {
            try {
                std::uint32_t type = 0, version = 0, length = 0;
                while (a_intfc->GetNextRecordInfo(type, version, length)) {
                    if (type == kGeminationRecord) Gemination::Load(a_intfc, version);
                }
            } catch (const std::exception& e) {
                SKSE::log::error("[Serialization] Loading failed: {}", e.what());
            }
        }
    }

    void Register()
    {
        auto* serialization = SKSE::GetSerializationInterface();
        serialization->SetUniqueID(kSerializationId);
        serialization->SetSaveCallback(SaveCallback);
        serialization->SetRevertCallback(RevertCallback);
        serialization->SetLoadCallback(LoadCallback);
    }

}
