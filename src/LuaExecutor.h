/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: LuaExecutor.h
 * Role: Lua callback execution abstraction used by runtime dispatch code so execution backends can be swapped
 * without changing callback call sites.
 */

#ifndef QMUD_LUAEXECUTOR_H
#define QMUD_LUAEXECUTOR_H

// ReSharper disable once CppUnusedIncludeDirective
#include <QByteArray>
// ReSharper disable once CppUnusedIncludeDirective
#include <QMap>
// ReSharper disable once CppUnusedIncludeDirective
#include <QSet>
#include <QString>
// ReSharper disable once CppUnusedIncludeDirective
#include <QStringList>
// ReSharper disable once CppUnusedIncludeDirective
#include <QVector>
#include <functional>
#include <memory>

class LuaCallbackEngine;
class WorldRuntime;
struct LuaStyleRun;
#ifdef QMUD_ENABLE_LUA_SCRIPTING
struct lua_State;
#endif

/**
 * @brief Execution seam for invoking Lua callback engine operations.
 *
 * Production currently uses @ref LuaExecutorDirect (same-thread direct dispatch),
 * while future threaded backends can implement this interface with identical
 * call semantics.
 */
class ILuaExecutor
{
	public:
		virtual ~ILuaExecutor() = default;

		/**
		 * @brief Binds runtime context into an engine.
		 */
		virtual void setWorldRuntime(LuaCallbackEngine *engine, WorldRuntime *runtime) const = 0;
		/**
		 * @brief Sets plugin identity metadata for an engine.
		 */
		virtual void setPluginInfo(LuaCallbackEngine *engine, const QString &id,
		                           const QString &name) const = 0;
		/**
		 * @brief Replaces script text in an engine.
		 */
		virtual void setScriptText(LuaCallbackEngine *engine, const QString &scriptText) const = 0;
		/**
		 * @brief Resets script state for an engine.
		 */
		virtual void resetState(LuaCallbackEngine *engine) const = 0;
		/**
		 * @brief Tears down engine runtime bindings and Lua state for safe destruction.
		 */
		virtual void teardownEngine(LuaCallbackEngine *engine) const = 0;
		/**
		 * @brief Applies package-restriction policy for an engine.
		 */
		virtual void applyPackageRestrictions(LuaCallbackEngine *engine, bool enablePackage) const = 0;
		/**
		 * @brief Sets observed callback catalog names for cache generation.
		 */
		virtual void setObservedPluginCallbacks(LuaCallbackEngine   *engine,
		                                        const QSet<QString> &callbackNames) const = 0;
		/**
		 * @brief Checks whether observed callback is present in cached catalog.
		 */
		virtual bool hasObservedPluginCallback(LuaCallbackEngine *engine,
		                                       const QString     &functionName) const = 0;
		/**
		 * @brief Sets callback-catalog observer hook.
		 */
		virtual void setCallbackCatalogObserver(LuaCallbackEngine    *engine,
		                                        std::function<void()> observer) const = 0;
		/**
		 * @brief Loads script state for an engine.
		 * @param engine Target callback engine.
		 * @return `true` when script is ready.
		 */
		virtual bool loadScript(LuaCallbackEngine *engine) const = 0;
		/**
		 * @brief Checks for function presence.
		 * @param engine Target callback engine.
		 * @param functionName Callback function name.
		 * @return `true` when function exists.
		 */
		virtual bool hasFunction(LuaCallbackEngine *engine, const QString &functionName) const = 0;
		/**
		 * @brief Invokes no-arg callback.
		 */
		virtual bool callFunctionNoArgs(LuaCallbackEngine *engine, const QString &functionName,
		                                bool *hasFunction, bool defaultResult) const = 0;
		/**
		 * @brief Invokes one-string callback.
		 */
		virtual bool callFunctionWithString(LuaCallbackEngine *engine, const QString &functionName,
		                                    const QString &arg, bool *hasFunction,
		                                    bool defaultResult) const = 0;
		/**
		 * @brief Invokes one-bytes callback.
		 */
		virtual bool callFunctionWithBytes(LuaCallbackEngine *engine, const QString &functionName,
		                                   const QByteArray &arg, bool *hasFunction,
		                                   bool defaultResult) const = 0;
		/**
		 * @brief Invokes in/out bytes callback.
		 */
		virtual bool callFunctionWithBytesInOut(LuaCallbackEngine *engine, const QString &functionName,
		                                        QByteArray &arg, bool *hasFunction) const = 0;
		/**
		 * @brief Invokes in/out string callback.
		 */
		virtual bool callFunctionWithStringInOut(LuaCallbackEngine *engine, const QString &functionName,
		                                         QString &arg, bool *hasFunction) const = 0;
		/**
		 * @brief Invokes number+string callback.
		 */
		virtual bool callFunctionWithNumberAndString(LuaCallbackEngine *engine, const QString &functionName,
		                                             long arg1, const QString &arg2, bool *hasFunction,
		                                             bool defaultResult) const = 0;
		/**
		 * @brief Invokes two-number+string callback.
		 */
		virtual bool callFunctionWithTwoNumbersAndString(LuaCallbackEngine *engine,
		                                                 const QString &functionName, long arg1, long arg2,
		                                                 const QString &arg3, bool *hasFunction,
		                                                 bool defaultResult) const = 0;
		/**
		 * @brief Invokes number+bytes callback.
		 */
		virtual bool callFunctionWithNumberAndBytes(LuaCallbackEngine *engine, const QString &functionName,
		                                            long arg1, const QByteArray &arg2, bool *hasFunction,
		                                            bool defaultResult) const = 0;
		/**
		 * @brief Invokes number+UTF-8-string triplet callback.
		 */
		virtual bool callFunctionWithNumberAndUtf8Strings(LuaCallbackEngine *engine,
		                                                  const QString &functionName, long arg1,
		                                                  const QByteArray &arg2Utf8,
		                                                  const QByteArray &arg3Utf8,
		                                                  const QByteArray &arg4Utf8, bool *hasFunction,
		                                                  bool defaultResult) const = 0;
		/**
		 * @brief Invokes one-string procedure callback.
		 */
		virtual bool callProcedureWithString(LuaCallbackEngine *engine, const QString &functionName,
		                                     const QString &arg, bool *hasFunction) const = 0;
		/**
		 * @brief Invokes MXP error callback.
		 */
		virtual bool callMxpError(LuaCallbackEngine *engine, const QString &functionName, int level,
		                          long messageNumber, int lineNumber, const QString &message) const = 0;
		/**
		 * @brief Invokes MXP startup callback.
		 */
		virtual void callMxpStartUp(LuaCallbackEngine *engine, const QString &functionName) const = 0;
		/**
		 * @brief Invokes MXP shutdown callback.
		 */
		virtual void callMxpShutDown(LuaCallbackEngine *engine, const QString &functionName) const = 0;
		/**
		 * @brief Invokes MXP start-tag callback.
		 */
		virtual bool callMxpStartTag(LuaCallbackEngine *engine, const QString &functionName,
		                             const QString &name, const QString &args,
		                             const QMap<QString, QString> &table) const = 0;
		/**
		 * @brief Invokes MXP end-tag callback.
		 */
		virtual void callMxpEndTag(LuaCallbackEngine *engine, const QString &functionName,
		                           const QString &name, const QString &text) const = 0;
		/**
		 * @brief Invokes MXP variable callback.
		 */
		virtual void callMxpSetVariable(LuaCallbackEngine *engine, const QString &functionName,
		                                const QString &name, const QString &contents) const = 0;
		/**
		 * @brief Invokes callback with argument list/wildcards and optional style runs.
		 */
		virtual bool callFunctionWithStringsAndWildcards(LuaCallbackEngine *engine,
		                                                 const QString &functionName, const QStringList &args,
		                                                 const QStringList            &wildcards,
		                                                 const QMap<QString, QString> &namedWildcards,
		                                                 const QVector<LuaStyleRun>   *styleRuns,
		                                                 bool                         *hasFunction) const = 0;
		/**
		 * @brief Executes arbitrary Lua code chunk.
		 */
		virtual bool executeScript(LuaCallbackEngine *engine, const QString &code, const QString &description,
		                           const QVector<LuaStyleRun> *styleRuns) const = 0;
		/**
		 * @brief Refreshes callback-catalog presence cache.
		 */
		virtual void refreshLuaCallbackCatalogNow(LuaCallbackEngine *engine) const = 0;
#ifdef QMUD_ENABLE_LUA_SCRIPTING
		/**
		 * @brief Returns Lua state pointer for an engine.
		 */
		virtual lua_State *luaState(LuaCallbackEngine *engine) const = 0;
#endif
};

/**
 * @brief Same-thread/direct execution backend that preserves current behavior.
 */
class LuaExecutorDirect final : public ILuaExecutor
{
	public:
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
};

/**
 * @brief Creates the runtime Lua executor backend.
 *
 * Currently, returns the direct/same-thread backend; future threading work will
 * switch this factory based on configuration once parity gates are satisfied.
 */
std::unique_ptr<ILuaExecutor> makeLuaExecutor();

#endif // QMUD_LUAEXECUTOR_H
