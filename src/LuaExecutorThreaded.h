/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: LuaExecutorThreaded.h
 * Role: Experimental worker-thread Lua executor backend scaffold (not default-enabled).
 */

#ifndef QMUD_LUAEXECUTORTHREADED_H
#define QMUD_LUAEXECUTORTHREADED_H

#include "LuaExecutor.h"

#include <QObject>
#include <QThread>

enum class LuaExecutorOperation;

/**
 * @brief Experimental synchronous worker-thread executor backend for Lua callback dispatch.
 *
 * This backend keeps call semantics synchronous by marshaling every engine operation
 * through a dedicated worker `QThread` and waiting with a local event loop.
 * It is intentionally gated behind an off-by-default build switch until parity and
 * re-entrant bridge constraints are fully validated.
 */
class LuaExecutorThreaded final : public ILuaExecutor
{
	public:
		LuaExecutorThreaded();
		~LuaExecutorThreaded() override;

		void setWorldRuntime(LuaCallbackEngine *engine, WorldRuntime *runtime) const override;
		void setPluginInfo(LuaCallbackEngine *engine, const QString &id, const QString &name) const override;
		void setScriptText(LuaCallbackEngine *engine, const QString &scriptText) const override;
		void resetState(LuaCallbackEngine *engine) const override;
		void teardownEngine(LuaCallbackEngine *engine) const override;
		void applyPackageRestrictions(LuaCallbackEngine *engine, bool enablePackage) const override;
		void setObservedPluginCallbacks(LuaCallbackEngine   *engine,
		                                const QSet<QString> &callbackNames) const override;
		bool hasObservedPluginCallback(LuaCallbackEngine *engine, const QString &functionName) const override;
		void setCallbackCatalogObserver(LuaCallbackEngine    *engine,
		                                std::function<void()> observer) const override;
		bool loadScript(LuaCallbackEngine *engine) const override;
		bool hasFunction(LuaCallbackEngine *engine, const QString &functionName) const override;
		bool callFunctionNoArgs(LuaCallbackEngine *engine, const QString &functionName, bool *hasFunction,
		                        bool defaultResult) const override;
		bool callFunctionWithString(LuaCallbackEngine *engine, const QString &functionName,
		                            const QString &arg, bool *hasFunction, bool defaultResult) const override;
		bool callFunctionWithBytes(LuaCallbackEngine *engine, const QString &functionName,
		                           const QByteArray &arg, bool *hasFunction,
		                           bool defaultResult) const override;
		bool callFunctionWithBytesInOut(LuaCallbackEngine *engine, const QString &functionName,
		                                QByteArray &arg, bool *hasFunction) const override;
		bool callFunctionWithStringInOut(LuaCallbackEngine *engine, const QString &functionName, QString &arg,
		                                 bool *hasFunction) const override;
		bool callFunctionWithNumberAndString(LuaCallbackEngine *engine, const QString &functionName,
		                                     long arg1, const QString &arg2, bool *hasFunction,
		                                     bool defaultResult) const override;
		bool callFunctionWithTwoNumbersAndString(LuaCallbackEngine *engine, const QString &functionName,
		                                         long arg1, long arg2, const QString &arg3, bool *hasFunction,
		                                         bool defaultResult) const override;
		bool callFunctionWithNumberAndBytes(LuaCallbackEngine *engine, const QString &functionName, long arg1,
		                                    const QByteArray &arg2, bool *hasFunction,
		                                    bool defaultResult) const override;
		bool callFunctionWithNumberAndUtf8Strings(LuaCallbackEngine *engine, const QString &functionName,
		                                          long arg1, const QByteArray &arg2Utf8,
		                                          const QByteArray &arg3Utf8, const QByteArray &arg4Utf8,
		                                          bool *hasFunction, bool defaultResult) const override;
		bool callProcedureWithString(LuaCallbackEngine *engine, const QString &functionName,
		                             const QString &arg, bool *hasFunction) const override;
		bool callMxpError(LuaCallbackEngine *engine, const QString &functionName, int level,
		                  long messageNumber, int lineNumber, const QString &message) const override;
		void callMxpStartUp(LuaCallbackEngine *engine, const QString &functionName) const override;
		void callMxpShutDown(LuaCallbackEngine *engine, const QString &functionName) const override;
		bool callMxpStartTag(LuaCallbackEngine *engine, const QString &functionName, const QString &name,
		                     const QString &args, const QMap<QString, QString> &table) const override;
		void callMxpEndTag(LuaCallbackEngine *engine, const QString &functionName, const QString &name,
		                   const QString &text) const override;
		void callMxpSetVariable(LuaCallbackEngine *engine, const QString &functionName, const QString &name,
		                        const QString &contents) const override;
		bool callFunctionWithStringsAndWildcards(LuaCallbackEngine *engine, const QString &functionName,
		                                         const QStringList &args, const QStringList &wildcards,
		                                         const QMap<QString, QString> &namedWildcards,
		                                         const QVector<LuaStyleRun>   *styleRuns,
		                                         bool                         *hasFunction) const override;
		bool executeScript(LuaCallbackEngine *engine, const QString &code, const QString &description,
		                   const QVector<LuaStyleRun> *styleRuns) const override;
		void refreshLuaCallbackCatalogNow(LuaCallbackEngine *engine) const override;
#ifdef QMUD_ENABLE_LUA_SCRIPTING
		lua_State *luaState(LuaCallbackEngine *engine) const override;
#endif

	private:
		void invokeBlockingVoid(const std::function<void()> &fn) const;
		bool invokeBlockingBool(const std::function<bool()> &fn, bool fallback) const;
		void invokeBlockingPolicyVoid(LuaExecutorOperation operation, const std::function<void()> &fn) const;
		bool invokeBlockingPolicyBool(LuaExecutorOperation operation, const std::function<bool()> &fn,
		                              bool fallback) const;
#ifdef QMUD_ENABLE_LUA_SCRIPTING
		lua_State *invokeBlockingLuaState(const std::function<lua_State *()> &fn) const;
		lua_State *invokeBlockingPolicyLuaState(const std::function<lua_State *()> &fn) const;
#endif
		static void              warnIfPolicyNotWorkerLocal(LuaExecutorOperation operation);
		void                     shutdownWorker();

		std::unique_ptr<QThread> m_workerThread;
		std::unique_ptr<QObject> m_workerInvoker;
		LuaExecutorDirect        m_direct;
};

#endif // QMUD_LUAEXECUTORTHREADED_H
