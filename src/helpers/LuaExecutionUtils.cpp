/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: LuaExecutionUtils.cpp
 * Role: Shared Lua execution helpers for backend mode selection, thread-affinity assertions,
 * and CallPlugin Lua marshalling.
 */

#include "helpers/LuaExecutionUtils.h"

#ifdef QMUD_ENABLE_LUA_SCRIPTING
#include "LuaHeaders.h"
#endif

// ReSharper disable once CppUnusedIncludeDirective
#include <QStringList>
#include <QThread>
#include <QtGlobal>

namespace
{
#ifdef QMUD_ENABLE_LUA_SCRIPTING
	bool pushNestedFunction(lua_State *state, const QString &dottedName)
	{
		const QStringList parts = dottedName.split(QLatin1Char('.'), Qt::SkipEmptyParts);
		if (parts.isEmpty())
			return false;
		const QByteArray first = parts.first().toLocal8Bit();
		lua_getglobal(state, first.constData());
		if (parts.size() == 1)
			return lua_isfunction(state, -1);
		if (!lua_istable(state, -1))
		{
			lua_pop(state, 1);
			return false;
		}
		for (int i = 1; i < parts.size(); ++i)
		{
			const QByteArray field = parts.at(i).toLocal8Bit();
			lua_getfield(state, -1, field.constData());
			lua_remove(state, -2);
			if (i < parts.size() - 1)
			{
				if (!lua_istable(state, -1))
				{
					lua_pop(state, 1);
					return false;
				}
			}
		}
		if (!lua_isfunction(state, -1))
		{
			lua_pop(state, 1);
			return false;
		}
		return true;
	}
#endif
} // namespace

void qmudAssertObjectThreadAffinity(const QObject *object, const char *context)
{
#if !defined(NDEBUG)
	Q_ASSERT_X(!object || object->thread() == QThread::currentThread(), context,
	           "QObject accessed from non-owning thread");
#else
	Q_UNUSED(object);
	Q_UNUSED(context);
#endif
}

bool qmudIsThreadedLuaExecutorRequested(const QByteArray &envValue)
{
	const QByteArray normalized = envValue.trimmed().toLower();
	return normalized == "threaded" || normalized == "worker";
}

LuaExecutorBackendMode qmudResolveLuaExecutorBackendMode(const QByteArray &envValue,
                                                         const bool        threadedBackendAvailable)
{
	if (threadedBackendAvailable && qmudIsThreadedLuaExecutorRequested(envValue))
		return LuaExecutorBackendMode::Threaded;
	return LuaExecutorBackendMode::Direct;
}

LuaExecutorBridgePolicy qmudLuaExecutorBridgePolicy(const LuaExecutorOperation operation)
{
	switch (operation)
	{
	case LuaExecutorOperation::SetWorldRuntime:
	case LuaExecutorOperation::SetPluginInfo:
	case LuaExecutorOperation::SetScriptText:
	case LuaExecutorOperation::ResetState:
	case LuaExecutorOperation::TeardownEngine:
	case LuaExecutorOperation::ApplyPackageRestrictions:
	case LuaExecutorOperation::SetObservedPluginCallbacks:
	case LuaExecutorOperation::HasObservedPluginCallback:
	case LuaExecutorOperation::SetCallbackCatalogObserver:
	case LuaExecutorOperation::LoadScript:
	case LuaExecutorOperation::HasFunction:
	case LuaExecutorOperation::RefreshLuaCallbackCatalogNow:
	case LuaExecutorOperation::LuaState:
		return LuaExecutorBridgePolicy::WorkerLocal;

	case LuaExecutorOperation::CallFunctionNoArgs:
	case LuaExecutorOperation::CallFunctionWithString:
	case LuaExecutorOperation::CallFunctionWithBytes:
	case LuaExecutorOperation::CallFunctionWithBytesInOut:
	case LuaExecutorOperation::CallFunctionWithStringInOut:
	case LuaExecutorOperation::CallFunctionWithNumberAndString:
	case LuaExecutorOperation::CallFunctionWithTwoNumbersAndString:
	case LuaExecutorOperation::CallFunctionWithNumberAndBytes:
	case LuaExecutorOperation::CallFunctionWithNumberAndUtf8Strings:
	case LuaExecutorOperation::CallProcedureWithString:
	case LuaExecutorOperation::CallMxpError:
	case LuaExecutorOperation::CallMxpStartUp:
	case LuaExecutorOperation::CallMxpShutDown:
	case LuaExecutorOperation::CallMxpStartTag:
	case LuaExecutorOperation::CallMxpEndTag:
	case LuaExecutorOperation::CallMxpSetVariable:
	case LuaExecutorOperation::CallFunctionWithStringsAndWildcards:
	case LuaExecutorOperation::ExecuteScript:
		return LuaExecutorBridgePolicy::UiSync;
	}
	return LuaExecutorBridgePolicy::UiSync;
}

CallPluginLuaMarshallingResult qmudCallPluginLuaWithMarshalling(lua_State     *callerState,
                                                                lua_State     *targetState,
                                                                const QString &routine, const int firstArg)
{
#ifdef QMUD_ENABLE_LUA_SCRIPTING
	CallPluginLuaMarshallingResult result;
	if (!callerState || !targetState)
	{
		result.error = CallPluginLuaMarshallingError::NoSuchRoutine;
		return result;
	}

	const int top      = lua_gettop(callerState);
	const int argCount = qMax(0, top - firstArg + 1);
	if (targetState == callerState)
	{
		if (!pushNestedFunction(targetState, routine))
		{
			result.error = CallPluginLuaMarshallingError::NoSuchRoutine;
			return result;
		}

		lua_insert(targetState, firstArg);
		if (lua_pcall(targetState, argCount, LUA_MULTRET, 0) != 0)
		{
			const char *err     = lua_tostring(targetState, -1);
			result.error        = CallPluginLuaMarshallingError::RuntimeError;
			result.runtimeError = QString::fromUtf8(err ? err : "unknown");
			lua_pop(targetState, 1);
			return result;
		}

		result.returnCount = lua_gettop(targetState) - firstArg + 1;
		return result;
	}

	lua_settop(targetState, 0);
	if (!pushNestedFunction(targetState, routine))
	{
		result.error = CallPluginLuaMarshallingError::NoSuchRoutine;
		return result;
	}

	lua_checkstack(targetState, argCount + 2);
	for (int i = firstArg; i <= top; ++i)
	{
		switch (const int type = lua_type(callerState, i); type)
		{
		case LUA_TNIL:
			lua_pushnil(targetState);
			break;
		case LUA_TBOOLEAN:
			lua_pushboolean(targetState, lua_toboolean(callerState, i));
			break;
		case LUA_TNUMBER:
			lua_pushnumber(targetState, lua_tonumber(callerState, i));
			break;
		case LUA_TSTRING:
		{
			size_t      len = 0;
			const char *s   = lua_tolstring(callerState, i, &len);
			lua_pushlstring(targetState, s, len);
			break;
		}
		default:
		{
			result.error    = CallPluginLuaMarshallingError::UnsupportedArgumentType;
			result.index    = i;
			result.typeName = lua_typename(callerState, type);
			lua_settop(targetState, 0);
			return result;
		}
		}
	}

	if (lua_pcall(targetState, argCount, LUA_MULTRET, 0) != 0)
	{
		const char *err     = lua_tostring(targetState, -1);
		result.error        = CallPluginLuaMarshallingError::RuntimeError;
		result.runtimeError = QString::fromUtf8(err ? err : "unknown");
		lua_settop(targetState, 0);
		return result;
	}

	const int retCount = lua_gettop(targetState);
	for (int i = 1; i <= retCount; ++i)
	{
		switch (const int type = lua_type(targetState, i); type)
		{
		case LUA_TNIL:
			lua_pushnil(callerState);
			break;
		case LUA_TBOOLEAN:
			lua_pushboolean(callerState, lua_toboolean(targetState, i));
			break;
		case LUA_TNUMBER:
			lua_pushnumber(callerState, lua_tonumber(targetState, i));
			break;
		case LUA_TSTRING:
		{
			size_t      len = 0;
			const char *s   = lua_tolstring(targetState, i, &len);
			lua_pushlstring(callerState, s, len);
			break;
		}
		default:
		{
			result.error    = CallPluginLuaMarshallingError::UnsupportedReturnType;
			result.index    = i;
			result.typeName = lua_typename(targetState, type);
			lua_settop(targetState, 0);
			return result;
		}
		}
	}

	lua_settop(targetState, 0);
	result.returnCount = retCount;
	return result;
#else
	Q_UNUSED(callerState);
	Q_UNUSED(targetState);
	Q_UNUSED(routine);
	Q_UNUSED(firstArg);
	CallPluginLuaMarshallingResult result;
	result.error = CallPluginLuaMarshallingError::NoSuchRoutine;
	return result;
#endif
}
