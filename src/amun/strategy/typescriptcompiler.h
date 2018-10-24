/***************************************************************************
 *   Copyright 2018 Andreas Wendler, Paul Bergmann                                        *
 *   Robotics Erlangen e.V.                                                *
 *   http://www.robotics-erlangen.de/                                      *
 *   info@robotics-erlangen.de                                             *
 *                                                                         *
 *   This program is free software: you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation, either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifndef TYPESCRIPTCOMPILER_H
#define TYPESCRIPTCOMPILER_H

#include "abstractstrategyscript.h"
#include "node/library.h"
#include "v8.h"
#include "v8-profiler.h"

#include <map>
#include <memory>

#include <QString>

class TypescriptCompiler
{
public:
    TypescriptCompiler();
    ~TypescriptCompiler();

    void startCompiler(const QString &filename);
private:
    static void requireModule(const v8::FunctionCallbackInfo<v8::Value>& args);
    void registerRequireFunction(v8::Local<v8::ObjectTemplate> global);

    // Node library functions
    void createLibraryObjects();

private:
    v8::Isolate* m_isolate;
    v8::Global<v8::Context> m_context;

    std::map<QString, std::unique_ptr<Library>> m_libraryObjects;
};

#endif // TYPESCRIPTCOMPILER_H
