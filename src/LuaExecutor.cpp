/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: LuaExecutor.cpp
 * Role: Direct Lua callback execution backend implementation for ILuaExecutor seam.
 */

#include "LuaExecutor.h"

#include "LuaCallbackEngine.h"
#include "LuaExecutorThreaded.h"

#include <QByteArray>
#include <QDebug>

#include <utility>

void LuaExecutorDirect::setWorldRuntime(LuaCallbackEngine *engine, WorldRuntime *runtime) const
{
	if (engine)
		engine->setWorldRuntime(runtime);
}

void LuaExecutorDirect::setPluginInfo(LuaCallbackEngine *engine, const QString &id, const QString &name) const
{
	if (engine)
		engine->setPluginInfo(id, name);
}

void LuaExecutorDirect::setScriptText(LuaCallbackEngine *engine, const QString &scriptText) const
{
	if (engine)
		engine->setScriptText(scriptText);
}

void LuaExecutorDirect::resetState(LuaCallbackEngine *engine) const
{
	if (engine)
		engine->resetState();
}

void LuaExecutorDirect::teardownEngine(LuaCallbackEngine *engine) const
{
	if (!engine)
		return;
	engine->setCallbackCatalogObserver({});
	engine->setWorldRuntime(nullptr);
	engine->resetState();
	engine->clearExecutionThreadAffinity();
}

void LuaExecutorDirect::applyPackageRestrictions(LuaCallbackEngine *engine, const bool enablePackage) const
{
	if (engine)
		engine->applyPackageRestrictions(enablePackage);
}

void LuaExecutorDirect::setObservedPluginCallbacks(LuaCallbackEngine   *engine,
                                                   const QSet<QString> &callbackNames) const
{
	if (engine)
		engine->setObservedPluginCallbacks(callbackNames);
}

bool LuaExecutorDirect::hasObservedPluginCallback(LuaCallbackEngine *engine,
                                                  const QString     &functionName) const
{
	return engine ? engine->hasObservedPluginCallback(functionName) : false;
}

void LuaExecutorDirect::setCallbackCatalogObserver(LuaCallbackEngine    *engine,
                                                   std::function<void()> observer) const
{
	if (engine)
		engine->setCallbackCatalogObserver(std::move(observer));
}

bool LuaExecutorDirect::loadScript(LuaCallbackEngine *engine) const
{
	return engine ? engine->loadScript() : false;
}

bool LuaExecutorDirect::hasFunction(LuaCallbackEngine *engine, const QString &functionName) const
{
	return engine ? engine->hasFunction(functionName) : false;
}

bool LuaExecutorDirect::callFunctionNoArgs(LuaCallbackEngine *engine, const QString &functionName,
                                           bool *hasFunction, const bool defaultResult) const
{
	return engine ? engine->callFunctionNoArgs(functionName, hasFunction, defaultResult) : defaultResult;
}

bool LuaExecutorDirect::callFunctionWithString(LuaCallbackEngine *engine, const QString &functionName,
                                               const QString &arg, bool *hasFunction,
                                               const bool defaultResult) const
{
	return engine ? engine->callFunctionWithString(functionName, arg, hasFunction, defaultResult)
	              : defaultResult;
}

bool LuaExecutorDirect::callFunctionWithBytes(LuaCallbackEngine *engine, const QString &functionName,
                                              const QByteArray &arg, bool *hasFunction,
                                              const bool defaultResult) const
{
	return engine ? engine->callFunctionWithBytes(functionName, arg, hasFunction, defaultResult)
	              : defaultResult;
}

bool LuaExecutorDirect::callFunctionWithBytesInOut(LuaCallbackEngine *engine, const QString &functionName,
                                                   QByteArray &arg, bool *hasFunction) const
{
	return engine ? engine->callFunctionWithBytesInOut(functionName, arg, hasFunction) : true;
}

bool LuaExecutorDirect::callFunctionWithStringInOut(LuaCallbackEngine *engine, const QString &functionName,
                                                    QString &arg, bool *hasFunction) const
{
	return engine ? engine->callFunctionWithStringInOut(functionName, arg, hasFunction) : true;
}

bool LuaExecutorDirect::callFunctionWithNumberAndString(LuaCallbackEngine *engine,
                                                        const QString &functionName, const long arg1,
                                                        const QString &arg2, bool *hasFunction,
                                                        const bool defaultResult) const
{
	return engine
	           ? engine->callFunctionWithNumberAndString(functionName, arg1, arg2, hasFunction, defaultResult)
	           : defaultResult;
}

bool LuaExecutorDirect::callFunctionWithTwoNumbersAndString(LuaCallbackEngine *engine,
                                                            const QString &functionName, const long arg1,
                                                            const long arg2, const QString &arg3,
                                                            bool *hasFunction, const bool defaultResult) const
{
	return engine ? engine->callFunctionWithTwoNumbersAndString(functionName, arg1, arg2, arg3, hasFunction,
	                                                            defaultResult)
	              : defaultResult;
}

bool LuaExecutorDirect::callFunctionWithNumberAndBytes(LuaCallbackEngine *engine, const QString &functionName,
                                                       const long arg1, const QByteArray &arg2,
                                                       bool *hasFunction, const bool defaultResult) const
{
	return engine
	           ? engine->callFunctionWithNumberAndBytes(functionName, arg1, arg2, hasFunction, defaultResult)
	           : defaultResult;
}

bool LuaExecutorDirect::callFunctionWithNumberAndUtf8Strings(
    LuaCallbackEngine *engine, const QString &functionName, const long arg1, const QByteArray &arg2Utf8,
    const QByteArray &arg3Utf8, const QByteArray &arg4Utf8, bool *hasFunction, const bool defaultResult) const
{
	return engine ? engine->callFunctionWithNumberAndUtf8Strings(functionName, arg1, arg2Utf8, arg3Utf8,
	                                                             arg4Utf8, hasFunction, defaultResult)
	              : defaultResult;
}

bool LuaExecutorDirect::callProcedureWithString(LuaCallbackEngine *engine, const QString &functionName,
                                                const QString &arg, bool *hasFunction) const
{
	return engine ? engine->callProcedureWithString(functionName, arg, hasFunction) : false;
}

bool LuaExecutorDirect::callMxpError(LuaCallbackEngine *engine, const QString &functionName, const int level,
                                     const long messageNumber, const int lineNumber,
                                     const QString &message) const
{
	return engine ? engine->callMxpError(functionName, level, messageNumber, lineNumber, message) : false;
}

void LuaExecutorDirect::callMxpStartUp(LuaCallbackEngine *engine, const QString &functionName) const
{
	if (engine)
		engine->callMxpStartUp(functionName);
}

void LuaExecutorDirect::callMxpShutDown(LuaCallbackEngine *engine, const QString &functionName) const
{
	if (engine)
		engine->callMxpShutDown(functionName);
}

bool LuaExecutorDirect::callMxpStartTag(LuaCallbackEngine *engine, const QString &functionName,
                                        const QString &name, const QString &args,
                                        const QMap<QString, QString> &table) const
{
	return engine ? engine->callMxpStartTag(functionName, name, args, table) : false;
}

void LuaExecutorDirect::callMxpEndTag(LuaCallbackEngine *engine, const QString &functionName,
                                      const QString &name, const QString &text) const
{
	if (engine)
		engine->callMxpEndTag(functionName, name, text);
}

void LuaExecutorDirect::callMxpSetVariable(LuaCallbackEngine *engine, const QString &functionName,
                                           const QString &name, const QString &contents) const
{
	if (engine)
		engine->callMxpSetVariable(functionName, name, contents);
}

bool LuaExecutorDirect::callFunctionWithStringsAndWildcards(
    LuaCallbackEngine *engine, const QString &functionName, const QStringList &args,
    const QStringList &wildcards, const QMap<QString, QString> &namedWildcards,
    const QVector<LuaStyleRun> *styleRuns, bool *hasFunction) const
{
	return engine ? engine->callFunctionWithStringsAndWildcards(functionName, args, wildcards, namedWildcards,
	                                                            styleRuns, hasFunction)
	              : false;
}

bool LuaExecutorDirect::executeScript(LuaCallbackEngine *engine, const QString &code,
                                      const QString &description, const QVector<LuaStyleRun> *styleRuns) const
{
	return engine ? engine->executeScript(code, description, styleRuns) : false;
}

void LuaExecutorDirect::refreshLuaCallbackCatalogNow(LuaCallbackEngine *engine) const
{
	if (engine)
		engine->refreshLuaCallbackCatalogNow();
}

#ifdef QMUD_ENABLE_LUA_SCRIPTING
lua_State *LuaExecutorDirect::luaState(LuaCallbackEngine *engine) const
{
	return engine ? engine->luaState() : nullptr;
}
#endif

std::unique_ptr<ILuaExecutor> makeLuaExecutor()
{
#if QMUD_ENABLE_EXPERIMENTAL_THREADED_LUA_EXECUTOR
	const QByteArray backendRequest = qgetenv("QMUD_LUA_EXECUTOR_BACKEND").trimmed().toLower();
	if (backendRequest == "direct")
		return std::make_unique<LuaExecutorDirect>();
	if (backendRequest == "threaded" || backendRequest == "worker")
		return std::make_unique<LuaExecutorThreaded>();
	// Default to threaded backend when compiled in; explicit env request can still force direct.
	return std::make_unique<LuaExecutorThreaded>();
#else
	return std::make_unique<LuaExecutorDirect>();
#endif
}
