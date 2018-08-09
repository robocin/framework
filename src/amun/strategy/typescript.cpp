/***************************************************************************
 *   Copyright 2018 Andreas Wendler                                        *
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

#include "typescript.h"

#include <QFileInfo>
#include <QTextStream>
#include <QDebug>

#include "v8.h"
#include "libplatform/libplatform.h"
#include "js_amun.h"
#include "js_path.h"

using namespace v8;

Typescript::Typescript(const Timer *timer, StrategyType type, bool debugEnabled, bool refboxControlEnabled) :
    AbstractStrategyScript (timer, type, debugEnabled, refboxControlEnabled)
{
    Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = ArrayBuffer::Allocator::NewDefaultAllocator();
    m_isolate = Isolate::New(create_params);
    m_isolate->Enter();

    HandleScope handleScope(m_isolate);
    Local<ObjectTemplate> global = ObjectTemplate::New(m_isolate);
    registerAmunJsCallbacks(m_isolate, global, this);
    registerPathJsCallbacks(m_isolate, global, this);
    registerModuleResolver(global);
    Local<Context> context = Context::New(m_isolate, nullptr, global);
    m_context.Reset(m_isolate, context);
}

Typescript::~Typescript()
{
    // TODO: delete objects
    //m_context.Reset();
    //m_isolate->Dispose();
}

bool Typescript::canHandle(const QString &filename)
{
    QFileInfo file(filename);
    // TODO: js is only for temporary tests
    return file.fileName().split(".").last() == "js";
}

AbstractStrategyScript* Typescript::createStrategy(const Timer *timer, StrategyType type, bool debugEnabled, bool refboxControlEnabled)
{
    return new Typescript(timer, type, debugEnabled, refboxControlEnabled);
}

bool Typescript::loadScript(const QString &filename, const QString &entryPoint, const world::Geometry &geometry, const robot::Team &team)
{
    // TODO: factor this common code to a function in AbstractStrategyScript
    Q_ASSERT(m_filename.isNull());

    // startup strategy information
    m_filename = filename;
    m_name = "<no script>";
    // strategy modules are loaded relative to the init script
    m_baseDir = QFileInfo(m_filename).absoluteDir();

    m_geometry.CopyFrom(geometry);
    m_team.CopyFrom(team);
    takeDebugStatus();

    QFile file(filename);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString content = in.readAll();
        QByteArray contentBytes = content.toLatin1();

        HandleScope handleScope(m_isolate);
        Local<Context> context = Local<Context>::New(m_isolate, m_context);
        Context::Scope contextScope(context);

        Local<String> source = String::NewFromUtf8(m_isolate,
                                            contentBytes.data(), NewStringType::kNormal).ToLocalChecked();

        // Compile the source code.
        Local<Script> script;
        TryCatch tryCatch(m_isolate);
        if (!Script::Compile(context, source).ToLocal(&script)) {
            String::Utf8Value error(m_isolate, tryCatch.StackTrace(context).ToLocalChecked());
            m_errorMsg = "<font color=\"red\">" + QString(*error) + "</font>";
            return false;
        }

        Local<Object> exportsValue = v8::Object::New(m_isolate);
        Local<String> exportsName = String::NewFromUtf8(m_isolate, "exports", NewStringType::kNormal).ToLocalChecked();
        context->Global()->Set(context, exportsName, exportsValue);

        // execute the script once to get entrypoints etc.
        MaybeLocal<Value> maybeResult = script->Run(context);
        if (tryCatch.HasTerminated() || tryCatch.HasCaught()) {
            String::Utf8Value error(m_isolate, tryCatch.StackTrace(context).ToLocalChecked());
            m_errorMsg = "<font color=\"red\">" + QString(*error) + "</font>";
            return false;
        }
        Local<Value> result;
        if (!maybeResult.ToLocal(&result)) {
            // the script returns nothing
            m_errorMsg = "<font color=\"red\">No entrypoints defined!</font>";
            return false;
        }

        if (!result->IsObject()) {
            m_errorMsg = "<font color=\"red\">Script doesn't return an object!</font>";
            return false;
        }

        Local<Object> resultObject = result->ToObject(context).ToLocalChecked();
        Local<String> nameString = String::NewFromUtf8(m_isolate, "name", NewStringType::kNormal).ToLocalChecked();
        Local<String> entrypointsString = String::NewFromUtf8(m_isolate, "entrypoints", NewStringType::kNormal).ToLocalChecked();
        if (!resultObject->Has(nameString) || !resultObject->Has(entrypointsString)) {
            m_errorMsg = "<font color=\"red\">Script must return object containing 'name' and 'entrypoints'!</font>";
            return false;
        }

        // TODO: Has will be deprecated
        // TODO: Get will be deprecated
        Local<Value> maybeName = resultObject->Get(nameString);
        if (!maybeName->IsString()) {
            m_errorMsg = "<font color=\"red\">Script name must be a string!</font>";
            return false;
        }
        Local<String> name = maybeName->ToString(context).ToLocalChecked();
        m_name = QString(*String::Utf8Value(name));
        // TODO: get options from strategy

        Local<Value> maybeEntryPoints = resultObject->Get(entrypointsString);
        if (!maybeEntryPoints->IsObject()) {
            m_errorMsg = "<font color=\"red\">Entrypoints must be an object!</font>";
            return false;
        }

        m_entryPoints.clear();
        QMap<QString, Local<Function>> entryPoints;
        Local<Object> entrypointsObject = maybeEntryPoints->ToObject(context).ToLocalChecked();
        Local<Array> properties = entrypointsObject->GetOwnPropertyNames();
        for (unsigned int i = 0;i<properties->Length();i++) {
            Local<Value> key = properties->Get(i);
            Local<Value> value = entrypointsObject->Get(key);
            if (!value->IsFunction()) {
                m_errorMsg = "<font color=\"red\">Entrypoints must contain functions!</font>";
                return false;
            }
            Local<Function> function = Local<Function>::Cast(value);

            QString keyString(*String::Utf8Value(key));
            m_entryPoints.append(keyString);
            entryPoints[keyString] = function;
        }

        if (!chooseEntryPoint(entryPoint)) {
            return false;
        }

        m_function.Reset(m_isolate, entryPoints[m_entryPoint]);
        return true;
    } else {
        m_errorMsg = "<font color=\"red\">Could not open file " + filename + "</font>";
        return false;
    }
}

void Typescript::performRequire(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    Isolate* isolate = args.GetIsolate();
    Typescript *t = static_cast<Typescript*>(Local<External>::Cast(args.Data())->Value());
    QString name = *String::Utf8Value(args[0]);
    if (!t->m_requireCache.contains(name)) {
        // TODO: may or may not work under windows
        QFileInfo initInfo(t->m_filename);
        QFileInfo fileInfo(name);
        QDir buildBaseDir = initInfo.absoluteDir();
        buildBaseDir.cdUp();
        QDir jsBaseDir = initInfo.absoluteDir();
        jsBaseDir.cd("../../..");
        QString relativePath = fileInfo.absolutePath().replace(jsBaseDir.absolutePath(), "");

        // for future use
        QFile file(buildBaseDir.absolutePath() + relativePath + "/" + fileInfo.fileName() + ".js");
        file.open(QIODevice::ReadOnly);
        QTextStream in(&file);
        QString content = in.readAll();
        QByteArray contentBytes = content.toLatin1();

        Local<String> source = String::NewFromUtf8(isolate,
                                            contentBytes.data(), NewStringType::kNormal).ToLocalChecked();
        Local<Context> context = isolate->GetCurrentContext();

        Local<Object> global = context->Global();
        Local<String> exportsName = String::NewFromUtf8(isolate, "exports", NewStringType::kNormal).ToLocalChecked();
        Local<Value> exportsBefore = global->Get(context, exportsName).ToLocalChecked();
        Local<Object> exportsValue = v8::Object::New(isolate);
        global->Set(context, exportsName, exportsValue);

        // Compile the source code.
        Local<Script> script;
        TryCatch tryCatch(isolate);
        if (!Script::Compile(context, source).ToLocal(&script)) {
            String::Utf8Value error(isolate, tryCatch.StackTrace(context).ToLocalChecked());
            return;
        }

        // execute the script once to get entrypoints etc.
        script->Run(context);
        global->Set(exportsName, exportsBefore);
        t->m_requireCache[name] = new Global<Value>(isolate, exportsValue);
    }
    args.GetReturnValue().Set(*t->m_requireCache[name]);
}

void Typescript::registerModuleResolver(Local<ObjectTemplate> global)
{
    Local<String> name = String::NewFromUtf8(m_isolate, "require", NewStringType::kNormal).ToLocalChecked();
    global->Set(name, FunctionTemplate::New(m_isolate, performRequire, External::New(m_isolate, this)));
}

void Typescript::addPathTime(double time)
{
    m_totalPathTime += time;
}

bool Typescript::process(double &pathPlanning, const world::State &worldState, const amun::GameState &refereeState, const amun::UserInput &userInput)
{
    Q_ASSERT(!m_entryPoint.isNull());

    m_worldState.CopyFrom(worldState);
    m_worldState.clear_vision_frames();
    m_refereeState.CopyFrom(refereeState);
    m_userInput.CopyFrom(userInput);
    takeDebugStatus();

    // TODO: script timeout
    m_totalPathTime = 0;

    HandleScope handleScope(m_isolate);
    Local<Context> context = Local<Context>::New(m_isolate, m_context);
    Context::Scope contextScope(context);

    TryCatch tryCatch(m_isolate);
    Local<Function> function = Local<Function>::New(m_isolate, m_function);
    function->Call(context, context->Global(), 0, nullptr);
    if (tryCatch.HasTerminated() || tryCatch.HasCaught()) {
        String::Utf8Value error(m_isolate, tryCatch.StackTrace(context).ToLocalChecked());
        m_errorMsg = "<font color=\"red\">" + QString(*error) + "</font>";
        return false;
    }
    pathPlanning = m_totalPathTime;
    return true;
}
