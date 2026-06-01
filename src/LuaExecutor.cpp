/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: LuaExecutor.cpp
 * Role: Direct Lua callback execution backend implementation for ILuaExecutor seam.
 */

#include "LuaExecutor.h"

#include "LuaCallbackEngine.h"
#include "LuaExecutorWorker.h"
#include "helpers/LuaExecutionUtils.h"

#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QScopeGuard>
#include <QThread>

#include <algorithm>
#include <utility>

LuaBatchDispatchResult ILuaExecutor::dispatchBatch(const LuaBatchDispatchRequest &request) const
{
	LuaBatchDispatchResult result;
	const auto             invokeForEngine = [&](LuaCallbackEngine *engine, auto &&fn)
	{
		if (!engine)
			return;
		if (request.miniWindowSnapshotArg)
		{
			engine->pushDispatchMiniWindowSnapshot(request.miniWindowSnapshotArg);
			const auto popSnapshot = qScopeGuard([engine] { engine->popDispatchMiniWindowSnapshot(); });
			fn(engine);
			result.deferredRuntimeMutationBatches += engine->takeDeferredRuntimeMutationBatches();
			return;
		}
		fn(engine);
		result.deferredRuntimeMutationBatches += engine->takeDeferredRuntimeMutationBatches();
	};
	const auto forEachEngine = [&](auto &&fn)
	{
		for (const auto &engine : request.engines)
		{
			invokeForEngine(engine.data(), fn);
		}
	};
	const auto withFirstEngine = [&](auto &&fn) -> bool
	{
		return std::ranges::any_of(request.engines,
		                           [&](const auto &engine)
		                           {
			                           if (!engine)
				                           return false;
			                           invokeForEngine(engine.data(), fn);
			                           return true;
		                           });
	};
	switch (request.kind)
	{
	case LuaBatchDispatchKind::NoArgs:
		result.boolResult  = true;
		result.hasFunction = false;
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    bool       hasFunction = false;
			    const bool ok =
			        engine->callFunctionNoArgs(request.functionName, &hasFunction, request.defaultResult);
			    result.boolResult  = result.boolResult && ok;
			    result.hasFunction = result.hasFunction || hasFunction;
		    });
		result.boolResultValid  = true;
		result.hasFunctionValid = true;
		return result;
	case LuaBatchDispatchKind::HasFunction:
	{
		result.hasFunction = false;
		static_cast<void>(
		    withFirstEngine([&](LuaCallbackEngine *engine)
		                    { result.hasFunction = engine->hasFunction(request.functionName); }));
		result.hasFunctionValid = true;
		return result;
	}
	case LuaBatchDispatchKind::String:
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    static_cast<void>(engine->callFunctionWithString(request.functionName, request.stringArg,
			                                                     nullptr, request.defaultResult));
		    });
		return result;
	case LuaBatchDispatchKind::StringStopOnFalse:
		result.boolResult = true;
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    bool       hasFunction = false;
			    const bool ok = engine->callFunctionWithString(request.functionName, request.stringArg,
			                                                   &hasFunction, request.defaultResult);
			    if (hasFunction && !ok)
				    result.boolResult = false;
		    });
		result.boolResultValid = true;
		return result;
	case LuaBatchDispatchKind::StringHandled:
		result.boolResult = false;
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    if (result.boolResult)
				    return;
			    bool hasFunction = false;
			    static_cast<void>(engine->callFunctionWithString(request.functionName, request.stringArg,
			                                                     &hasFunction, false));
			    // Legacy SendToFirstPluginCallbacks semantics: handled if callback exists,
			    // regardless of the callback's boolean return value.
			    if (hasFunction)
				    result.boolResult = true;
		    });
		result.boolResultValid = true;
		return result;
	case LuaBatchDispatchKind::Bytes:
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    static_cast<void>(engine->callFunctionWithBytes(request.functionName, request.bytesArg,
			                                                    nullptr, request.defaultResult));
		    });
		return result;
	case LuaBatchDispatchKind::BytesInOut:
		result.bytesResult = request.bytesArg;
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    static_cast<void>(
			        engine->callFunctionWithBytesInOut(request.functionName, result.bytesResult, nullptr));
		    });
		return result;
	case LuaBatchDispatchKind::StringInOut:
		result.stringResult = request.stringArg;
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    static_cast<void>(
			        engine->callFunctionWithStringInOut(request.functionName, result.stringResult, nullptr));
		    });
		return result;
	case LuaBatchDispatchKind::NumberAndStringStopOnTrue:
		result.boolResult = false;
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    if (result.boolResult)
				    return;
			    bool       hasFunction = false;
			    const bool ok          = engine->callFunctionWithNumberAndString(
			        request.functionName, request.numberArg1, request.stringArg2, &hasFunction,
			        request.defaultResult);
			    if (hasFunction && ok)
				    result.boolResult = true;
		    });
		result.boolResultValid = true;
		return result;
	case LuaBatchDispatchKind::NumberAndStringStopOnFalse:
		result.boolResult = true;
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    bool       hasFunction = false;
			    const bool ok          = engine->callFunctionWithNumberAndString(
			        request.functionName, request.numberArg1, request.stringArg2, &hasFunction,
			        request.defaultResult);
			    if (hasFunction && !ok)
				    result.boolResult = false;
		    });
		result.boolResultValid = true;
		return result;
	case LuaBatchDispatchKind::NumberAndString:
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    static_cast<void>(engine->callFunctionWithNumberAndString(
			        request.functionName, request.numberArg1, request.stringArg2, nullptr,
			        request.defaultResult));
		    });
		return result;
	case LuaBatchDispatchKind::TwoNumbersAndStringStopOnFalse:
		result.boolResult = true;
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    bool       hasFunction = false;
			    const bool ok          = engine->callFunctionWithTwoNumbersAndString(
			        request.functionName, request.numberArg1, request.numberArg2, request.stringArg2,
			        &hasFunction, request.defaultResult);
			    if (hasFunction && !ok)
				    result.boolResult = false;
		    });
		result.boolResultValid = true;
		return result;
	case LuaBatchDispatchKind::TwoNumbersAndString:
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    static_cast<void>(engine->callFunctionWithTwoNumbersAndString(
			        request.functionName, request.numberArg1, request.numberArg2, request.stringArg2, nullptr,
			        request.defaultResult));
		    });
		return result;
	case LuaBatchDispatchKind::NumberAndBytesStopOnTrue:
		result.boolResult = false;
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    if (result.boolResult)
				    return;
			    bool       hasFunction = false;
			    const bool ok = engine->callFunctionWithNumberAndBytes(request.functionName,
			                                                           request.numberArg1, request.bytesArg,
			                                                           &hasFunction, request.defaultResult);
			    if (hasFunction && ok)
				    result.boolResult = true;
		    });
		result.boolResultValid = true;
		return result;
	case LuaBatchDispatchKind::NumberAndBytes:
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    static_cast<void>(engine->callFunctionWithNumberAndBytes(request.functionName,
			                                                             request.numberArg1, request.bytesArg,
			                                                             nullptr, request.defaultResult));
		    });
		return result;
	case LuaBatchDispatchKind::NumberAndUtf8StringsCount:
		result.countResult = 0;
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    bool hasFunction = false;
			    static_cast<void>(engine->callFunctionWithNumberAndUtf8Strings(
			        request.functionName, request.numberArg1, request.bytesArg, request.bytesArg2,
			        request.bytesArg3, &hasFunction, request.defaultResult));
			    if (hasFunction)
				    ++result.countResult;
		    });
		result.countResultValid = true;
		return result;
	case LuaBatchDispatchKind::StringsAndWildcards:
		result.hasFunction      = false;
		result.hasFunctionValid = true;
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    bool hasFunction = false;
			    static_cast<void>(engine->callFunctionWithStringsAndWildcards(
			        request.functionName, request.stringListArg, request.stringListArg2, request.mapArg,
			        request.styleRunsArg ? request.styleRunsArg.data() : nullptr,
			        request.miniWindowSnapshotArg ? request.miniWindowSnapshotArg.data() : nullptr,
			        &hasFunction, request.hasActionSourceOverride ? request.actionSourceOverride : -1,
			        request.triggerOutputReplacesMatchedLine, request.triggerMatchedLineBufferIndex,
			        request.triggerMatchedLineAbsoluteNumber));
			    result.hasFunction = result.hasFunction || hasFunction;
		    });
		return result;
	case LuaBatchDispatchKind::ExecuteScript:
	{
		result.boolResult = false;
		static_cast<void>(withFirstEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    result.boolResult = engine->executeScript(
			        request.stringArg, request.stringArg2,
			        request.styleRunsArg ? request.styleRunsArg.data() : nullptr,
			        request.executeScriptHasTriggerContext, request.triggerOutputReplacesMatchedLine,
			        request.triggerMatchedLineBufferIndex, request.triggerMatchedLineAbsoluteNumber);
		    }));
		result.boolResultValid = true;
		return result;
	}
	case LuaBatchDispatchKind::ResetAndLoadScript:
	{
		result.boolResult = false;
		static_cast<void>(withFirstEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    engine->resetState();
			    result.boolResult = engine->loadScript();
		    }));
		result.boolResultValid = true;
		return result;
	}
	case LuaBatchDispatchKind::InitializeEnginesWithObservedCallbacksMany:
		if (request.initRequestsArg)
		{
			for (const LuaEngineObservedInitializationRequest &initRequest : *request.initRequestsArg)
			{
				if (!initRequest.engine)
					continue;
				initRequest.engine->setWorldRuntime(initRequest.runtime);
				initRequest.engine->setScriptText(initRequest.scriptText);
				if (!initRequest.pluginId.isEmpty() || !initRequest.pluginName.isEmpty() ||
				    !initRequest.pluginDirectory.isEmpty())
				{
					initRequest.engine->setPluginInfo(initRequest.pluginId, initRequest.pluginName,
					                                  initRequest.pluginDirectory);
				}
				initRequest.engine->setCallbackCatalogObserver(initRequest.observer);
				initRequest.engine->setObservedPluginCallbacks(initRequest.callbackNames);
			}
		}
		return result;
	case LuaBatchDispatchKind::UpdateObservedCallbacksMany:
		forEachEngine([&](LuaCallbackEngine *engine)
		              { engine->setObservedPluginCallbacks(request.observedCallbackNamesArg); });
		return result;
	case LuaBatchDispatchKind::TeardownEnginesMany:
		for (const auto &engine : request.engines)
		{
			if (!engine)
				continue;
			engine->setCallbackCatalogObserver({});
			engine->setWorldRuntime(nullptr);
			engine->resetState();
			result.deferredRuntimeMutationBatches += engine->takeDeferredRuntimeMutationBatches();
			engine->clearExecutionThreadAffinity();
		}
		return result;
	case LuaBatchDispatchKind::ApplyPackageRestrictionsMany:
		forEachEngine([&](LuaCallbackEngine *engine)
		              { engine->applyPackageRestrictions(request.optionFlag); });
		return result;
	case LuaBatchDispatchKind::CallPluginLuaMarshalling:
	{
#ifdef QMUD_ENABLE_LUA_SCRIPTING
		result.boolResultValid       = true;
		result.marshallingErrorValid = true;
		if (!request.luaStateArg)
		{
			result.boolResult       = false;
			result.marshallingError = static_cast<int>(CallPluginLuaMarshallingError::NoSuchRoutine);
			return result;
		}
		const bool dispatched = withFirstEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    if (!engine->loadScript())
			    {
				    result.boolResult       = false;
				    result.marshallingError = static_cast<int>(CallPluginLuaMarshallingError::NoSuchRoutine);
				    return;
			    }

			    if (request.applyCallingPluginContext)
				    engine->pushCallingPluginId(request.callingPluginId);
			    const auto restoreCallingContext = qScopeGuard(
			        [&]
			        {
				        if (request.applyCallingPluginContext)
					        engine->popCallingPluginId();
			        });
			    const CallPluginLuaMarshallingResult marshalling =
			        engine->callPluginLuaWithMarshalling(request.luaStateArg, request.functionName,
			                                             request.intArg1, {}, request.miniWindowSnapshotArg);
			    result.boolResult              = true;
			    lua_State *const targetState   = engine->luaState();
			    result.marshallingSameState    = (targetState == request.luaStateArg);
			    result.marshallingError        = static_cast<int>(marshalling.error);
			    result.marshallingIndex        = marshalling.index;
			    result.marshallingTypeName     = marshalling.typeName;
			    result.marshallingRuntimeError = marshalling.runtimeError;
			    result.marshallingReturnCount  = marshalling.returnCount;
			    if (request.refreshCallbackCatalogAfter)
				    engine->refreshLuaCallbackCatalogNow();
		    });
		if (!dispatched)
		{
			result.boolResult       = false;
			result.marshallingError = static_cast<int>(CallPluginLuaMarshallingError::NoSuchRoutine);
		}
		return result;
#else
		result.boolResult            = false;
		result.boolResultValid       = true;
		result.marshallingError      = static_cast<int>(CallPluginLuaMarshallingError::NoSuchRoutine);
		result.marshallingErrorValid = true;
		return result;
#endif
	}
	case LuaBatchDispatchKind::ProcedureWithString:
	{
		bool hasFunction  = false;
		result.boolResult = false;
		static_cast<void>(withFirstEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    if (request.applyCallingPluginContext)
				    engine->pushCallingPluginId(request.callingPluginId);
			    const auto restoreCallingContext = qScopeGuard(
			        [&]
			        {
				        if (request.applyCallingPluginContext)
					        engine->popCallingPluginId();
			        });
			    result.boolResult =
			        engine->callProcedureWithString(request.functionName, request.stringArg, &hasFunction);
		    }));
		result.boolResultValid  = true;
		result.hasFunction      = hasFunction;
		result.hasFunctionValid = true;
		return result;
	}
	case LuaBatchDispatchKind::MxpError:
	{
		result.boolResult = false;
		static_cast<void>(withFirstEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    result.boolResult =
			        engine->callMxpError(request.functionName, request.intArg1, request.numberArg1,
			                             request.intArg2, request.stringArg);
		    }));
		result.boolResultValid = true;
		return result;
	}
	case LuaBatchDispatchKind::MxpStartUp:
		forEachEngine([&](LuaCallbackEngine *engine) { engine->callMxpStartUp(request.functionName); });
		return result;
	case LuaBatchDispatchKind::MxpShutDown:
		forEachEngine([&](LuaCallbackEngine *engine) { engine->callMxpShutDown(request.functionName); });
		return result;
	case LuaBatchDispatchKind::MxpStartTag:
	{
		result.boolResult = false;
		static_cast<void>(withFirstEngine(
		    [&](LuaCallbackEngine *engine)
		    {
			    result.boolResult = engine->callMxpStartTag(request.functionName, request.stringArg,
			                                                request.stringArg2, request.mapArg);
		    }));
		result.boolResultValid = true;
		return result;
	}
	case LuaBatchDispatchKind::MxpEndTag:
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    { engine->callMxpEndTag(request.functionName, request.stringArg, request.stringArg2); });
		return result;
	case LuaBatchDispatchKind::MxpSetVariable:
		forEachEngine(
		    [&](LuaCallbackEngine *engine)
		    { engine->callMxpSetVariable(request.functionName, request.stringArg, request.stringArg2); });
		return result;
	}
	return result;
}

void ILuaExecutor::dispatchBatchAsync(const LuaBatchDispatchRequest &request) const
{
	static_cast<void>(dispatchBatch(request));
}

void ILuaExecutor::dispatchBatchAsync(
    const LuaBatchDispatchRequest &request, QObject *completionTarget,
    const std::function<void(const LuaBatchDispatchResult &)> &completion) const
{
	const LuaBatchDispatchResult result = dispatchBatch(request);
	if (!completion)
		return;
	if (!completionTarget || completionTarget->thread() == QThread::currentThread())
	{
		completion(result);
		return;
	}
	const QPointer<QObject> targetGuard(completionTarget);
	auto                    queuedCompletion = completion;
	const bool              queued           = QMetaObject::invokeMethod(
	    completionTarget,
	    [targetGuard, completion = std::move(queuedCompletion), result]() mutable
	    {
		    if (!targetGuard)
			    return;
		    completion(result);
	    },
	    Qt::QueuedConnection);
	if (!queued)
	{
		qWarning().noquote() << QStringLiteral(
		    "[QMud][LuaExecutor] failed to queue completion delivery to target thread");
	}
}

std::unique_ptr<ILuaExecutor> makeLuaExecutor()
{
#if QMUD_ENABLE_EXPERIMENTAL_THREADED_LUA_EXECUTOR
	return std::make_unique<LuaExecutorWorker>();
#else
	return std::make_unique<LuaExecutorDirect>();
#endif
}
