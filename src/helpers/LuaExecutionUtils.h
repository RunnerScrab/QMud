/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: LuaExecutionUtils.h
 * Role: Shared Lua execution helpers for backend mode selection, thread-affinity assertions,
 * and CallPlugin Lua marshalling.
 */

#ifndef QMUD_LUAEXECUTIONUTILS_H
#define QMUD_LUAEXECUTIONUTILS_H

// ReSharper disable once CppUnusedIncludeDirective
#include <QByteArray>
#include <QString>

class QObject;
struct lua_State;

/**
 * @brief Lua executor backend mode selector.
 */
enum class LuaExecutorBackendMode
{
	Direct,
	Threaded
};

/**
 * @brief Bridge policy used for Lua executor operations during threading migration.
 */
enum class LuaExecutorBridgePolicy
{
	WorkerLocal,
	UiSync,
	UiAsync
};

/**
 * @brief Operation identifiers used to classify executor bridge policy.
 */
enum class LuaExecutorOperation
{
	SetWorldRuntime,
	SetPluginInfo,
	SetScriptText,
	ResetState,
	TeardownEngine,
	ApplyPackageRestrictions,
	SetObservedPluginCallbacks,
	HasObservedPluginCallback,
	SetCallbackCatalogObserver,
	LoadScript,
	HasFunction,
	CallFunctionNoArgs,
	CallFunctionWithString,
	CallFunctionWithBytes,
	CallFunctionWithBytesInOut,
	CallFunctionWithStringInOut,
	CallFunctionWithNumberAndString,
	CallFunctionWithTwoNumbersAndString,
	CallFunctionWithNumberAndBytes,
	CallFunctionWithNumberAndUtf8Strings,
	CallProcedureWithString,
	CallMxpError,
	CallMxpStartUp,
	CallMxpShutDown,
	CallMxpStartTag,
	CallMxpEndTag,
	CallMxpSetVariable,
	CallFunctionWithStringsAndWildcards,
	ExecuteScript,
	RefreshLuaCallbackCatalogNow,
	LuaState
};

/**
 * @brief Returns whether environment value requests threaded Lua executor mode.
 * @param envValue Raw environment value (for example `QMUD_LUA_EXECUTOR_BACKEND`).
 * @return `true` when value equals `threaded` or `worker` (case-insensitive, trimmed).
 */
bool                    qmudIsThreadedLuaExecutorRequested(const QByteArray &envValue);

/**
 * @brief Resolves effective Lua executor backend mode.
 * @param envValue Raw environment value.
 * @param threadedBackendAvailable Whether threaded backend is compiled/available.
 * @return Effective backend mode (`Threaded` only when requested and available).
 */
LuaExecutorBackendMode  qmudResolveLuaExecutorBackendMode(const QByteArray &envValue,
                                                          bool              threadedBackendAvailable);

/**
 * @brief Returns bridge policy classification for one Lua executor operation.
 * @param operation Operation to classify.
 * @return Effective bridge policy for migration gating.
 */
LuaExecutorBridgePolicy qmudLuaExecutorBridgePolicy(LuaExecutorOperation operation);

/**
 * @brief Error classification for Lua CallPlugin marshaling/invocation.
 */
enum class CallPluginLuaMarshallingError
{
	None,
	NoSuchRoutine,
	UnsupportedArgumentType,
	RuntimeError,
	UnsupportedReturnType
};

/**
 * @brief Result of invoking/marshaling a Lua callback between caller/target states.
 */
struct CallPluginLuaMarshallingResult
{
		CallPluginLuaMarshallingError error{CallPluginLuaMarshallingError::None};
		int                           index{0};
		QByteArray                    typeName;
		QString                       runtimeError;
		int                           returnCount{0};
};

/**
 * @brief Calls a dotted Lua function name and marshals values between Lua states.
 * @param callerState Caller-side Lua state where arguments and final return values live.
 * @param targetState Target Lua state where routine is executed.
 * @param routine Dotted routine name (`foo.bar.baz`) to invoke.
 * @param firstArg 1-based caller stack index of first argument for routine call.
 * @return Classification + details of the invocation/marshaling result.
 */
CallPluginLuaMarshallingResult qmudCallPluginLuaWithMarshalling(lua_State     *callerState,
                                                                lua_State     *targetState,
                                                                const QString &routine, int firstArg);

/**
 * @brief Debug-only assertion that verifies an object is used from its owning Qt thread.
 * @param object Object whose affinity should match current thread.
 * @param context Assertion context label.
 */
void                           qmudAssertObjectThreadAffinity(const QObject *object, const char *context);

#endif // QMUD_LUAEXECUTIONUTILS_H
