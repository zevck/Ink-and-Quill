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

// book.swf, the open book menu's own movie (separate from bookmenu.swf), and calls into it.  UI thread.
namespace InkAndQuill::Book {

    inline RE::BookMenu* Menu()
    {
        auto* ui = RE::UI::GetSingleton();
        auto menu = ui ? ui->GetMenu<RE::BookMenu>() : nullptr;
        return menu.get();
    }

    inline RE::GFxMovieView* Movie()
    {
        auto* menu = Menu();
        return menu ? menu->GetRuntimeData().book.get() : nullptr;
    }

    // _root.BookMenu_mc.<function>(argument): false when there's no movie or no such function.
    inline bool Call(const char* function, RE::GFxValue* result = nullptr, const char* argument = nullptr)
    {
        auto* movie = Movie();
        if (!movie) return false;
        RE::GFxValue arg;
        if (argument) arg.SetString(argument);
        return movie->Invoke(std::format("_root.BookMenu_mc.{}", function).c_str(), result, argument ? &arg : nullptr,
                             argument ? 1 : 0);
    }

}
