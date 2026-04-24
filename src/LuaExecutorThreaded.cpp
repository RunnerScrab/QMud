/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: LuaExecutorThreaded.cpp
 * Role: Experimental worker-thread Lua executor backend scaffold implementation.
 */

#include "LuaExecutorThreaded.h"

#include "LuaCallbackEngine.h"
#include "helpers/LuaExecutionUtils.h"

#include <QDebug>

#include <type_traits>
#include <utility>

namespace
{
	const QString kWorkerBridgeFailurePrefix =
	    QStringLiteral("[QMud][LuaExecutor] worker bridge invoke failed");

	bool ensureWorkerReady(const QObject *invoker, QThread *workerThread)
	{
		if (!invoker || !workerThread)
			return false;
		if (!workerThread->isRunning())
			workerThread->start();
		return workerThread->isRunning();
	}

	bool ensureWorkerBridgeReady(const QObject *invoker, QThread *workerThread)
	{
		if (!ensureWorkerReady(invoker, workerThread))
			return false;
		if (qmudLuaBridgeEnsureObjectThreadReady(invoker))
			return true;
		qWarning().noquote() << QStringLiteral("[QMud][LuaExecutor] worker bridge readiness failed: %1")
		                            .arg(qmudLuaBridgeLastError());
		return false;
	}

	const char *bridgeStatusName(const LuaBridgeInvokeStatus status)
	{
		switch (status)
		{
		case LuaBridgeInvokeStatus::Success:
			return "Success";
		case LuaBridgeInvokeStatus::InvalidTargetOrCallback:
			return "InvalidTargetOrCallback";
		case LuaBridgeInvokeStatus::NoOwningThread:
			return "NoOwningThread";
		case LuaBridgeInvokeStatus::MissingTargetBridgeContext:
			return "MissingTargetBridgeContext";
		case LuaBridgeInvokeStatus::NoQCoreApplication:
			return "NoQCoreApplication";
		case LuaBridgeInvokeStatus::TargetThreadNotRunning:
			return "TargetThreadNotRunning";
		case LuaBridgeInvokeStatus::TargetThreadNoEventDispatcher:
			return "TargetThreadNoEventDispatcher";
		case LuaBridgeInvokeStatus::BridgeInvokerUnavailable:
			return "BridgeInvokerUnavailable";
		case LuaBridgeInvokeStatus::BridgeEndpointNotReadyTimeout:
			return "BridgeEndpointNotReadyTimeout";
		case LuaBridgeInvokeStatus::MissingCallerBridgeContext:
			return "MissingCallerBridgeContext";
		case LuaBridgeInvokeStatus::EnqueueFailed:
			return "EnqueueFailed";
		case LuaBridgeInvokeStatus::RequestCanceledBeforeDispatch:
			return "RequestCanceledBeforeDispatch";
		case LuaBridgeInvokeStatus::RequestCanceledTargetThreadStopped:
			return "RequestCanceledTargetThreadStopped";
		case LuaBridgeInvokeStatus::TimedOutInFlight:
			return "TimedOutInFlight";
		case LuaBridgeInvokeStatus::UnknownFailure:
			return "UnknownFailure";
		}
		Q_UNREACHABLE_RETURN("UnknownFailure");
	}

	bool shouldRetryWorkerBridgeInvoke(const LuaBridgeInvokeStatus status)
	{
		switch (status)
		{
		case LuaBridgeInvokeStatus::MissingTargetBridgeContext:
		case LuaBridgeInvokeStatus::TargetThreadNotRunning:
		case LuaBridgeInvokeStatus::TargetThreadNoEventDispatcher:
		case LuaBridgeInvokeStatus::BridgeInvokerUnavailable:
		case LuaBridgeInvokeStatus::BridgeEndpointNotReadyTimeout:
		case LuaBridgeInvokeStatus::EnqueueFailed:
		case LuaBridgeInvokeStatus::RequestCanceledBeforeDispatch:
		case LuaBridgeInvokeStatus::RequestCanceledTargetThreadStopped:
			return true;

		case LuaBridgeInvokeStatus::Success:
		case LuaBridgeInvokeStatus::InvalidTargetOrCallback:
		case LuaBridgeInvokeStatus::NoOwningThread:
		case LuaBridgeInvokeStatus::NoQCoreApplication:
		case LuaBridgeInvokeStatus::MissingCallerBridgeContext:
		case LuaBridgeInvokeStatus::TimedOutInFlight:
		case LuaBridgeInvokeStatus::UnknownFailure:
			return false;
		}
		Q_UNREACHABLE_RETURN(false);
	}

	template <typename ResultType, typename Fn>
	ResultType invokeBlockingResult(const QObject *invoker, QThread *workerThread, Fn &&fn,
	                                ResultType fallback)
	{
		if (QThread::currentThread() == workerThread)
			return fn();
		if (!ensureWorkerBridgeReady(invoker, workerThread))
			return fallback;

		const auto            fnCopy       = std::decay_t<Fn>(std::forward<Fn>(fn));
		ResultType            result       = fallback;
		LuaBridgeInvokeStatus failedStatus = LuaBridgeInvokeStatus::Success;
		QString               failedError;
		auto                  invokeOnce = [&]() -> bool
		{
			if (qmudLuaBridgeInvokeOnObjectThread(invoker, [&]() { result = fnCopy(); }))
				return true;
			failedStatus = qmudLuaBridgeLastStatus();
			failedError  = qmudLuaBridgeLastError();
			return false;
		};

		bool bridged = invokeOnce();
		if (!bridged && shouldRetryWorkerBridgeInvoke(failedStatus) &&
		    ensureWorkerBridgeReady(invoker, workerThread))
		{
			bridged = invokeOnce();
		}
		if (!bridged)
		{
			qWarning().noquote() << QStringLiteral("%1 (%2): %3")
			                            .arg(kWorkerBridgeFailurePrefix,
			                                 QString::fromLatin1(bridgeStatusName(failedStatus)),
			                                 failedError);
		}
		return bridged ? result : fallback;
	}

	template <typename Fn> void invokeBlockingVoid(const QObject *invoker, QThread *workerThread, Fn &&fn)
	{
		if (QThread::currentThread() == workerThread)
		{
			fn();
			return;
		}
		if (!ensureWorkerBridgeReady(invoker, workerThread))
			return;

		const auto            fnCopy       = std::decay_t<Fn>(std::forward<Fn>(fn));
		LuaBridgeInvokeStatus failedStatus = LuaBridgeInvokeStatus::Success;
		QString               failedError;
		auto                  invokeOnce = [&]() -> bool
		{
			if (qmudLuaBridgeInvokeOnObjectThread(invoker, [&]() { fnCopy(); }))
				return true;
			failedStatus = qmudLuaBridgeLastStatus();
			failedError  = qmudLuaBridgeLastError();
			return false;
		};
		bool bridged = invokeOnce();
		if (!bridged && shouldRetryWorkerBridgeInvoke(failedStatus) &&
		    ensureWorkerBridgeReady(invoker, workerThread))
		{
			bridged = invokeOnce();
		}
		if (!bridged)
		{
			qWarning().noquote() << QStringLiteral("%1 (%2): %3")
			                            .arg(kWorkerBridgeFailurePrefix,
			                                 QString::fromLatin1(bridgeStatusName(failedStatus)),
			                                 failedError);
		}
	}
} // namespace

LuaExecutorThreaded::LuaExecutorThreaded()
{
	m_workerThread = std::make_unique<QThread>();
	m_workerThread->setObjectName(QStringLiteral("QMudLuaExecutorWorker"));
	m_workerInvoker = std::make_unique<QObject>();
	m_workerInvoker->moveToThread(m_workerThread.get());
	m_workerThread->start();
	static_cast<void>(ensureWorkerBridgeReady(m_workerInvoker.get(), m_workerThread.get()));
}

LuaExecutorThreaded::~LuaExecutorThreaded()
{
	shutdownWorker();
}

void LuaExecutorThreaded::setWorldRuntime(LuaCallbackEngine *engine, WorldRuntime *runtime) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::SetWorldRuntime,
	                         [&] { m_direct.setWorldRuntime(engine, runtime); });
}

void LuaExecutorThreaded::setPluginInfo(LuaCallbackEngine *engine, const QString &id,
                                        const QString &name) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::SetPluginInfo,
	                         [&] { m_direct.setPluginInfo(engine, id, name); });
}

void LuaExecutorThreaded::setScriptText(LuaCallbackEngine *engine, const QString &scriptText) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::SetScriptText,
	                         [&] { m_direct.setScriptText(engine, scriptText); });
}

void LuaExecutorThreaded::resetState(LuaCallbackEngine *engine) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::ResetState, [&] { m_direct.resetState(engine); });
}

void LuaExecutorThreaded::teardownEngine(LuaCallbackEngine *engine) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::TeardownEngine, [&] { m_direct.teardownEngine(engine); });
}

void LuaExecutorThreaded::applyPackageRestrictions(LuaCallbackEngine *engine, const bool enablePackage) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::ApplyPackageRestrictions,
	                         [&] { m_direct.applyPackageRestrictions(engine, enablePackage); });
}

void LuaExecutorThreaded::setObservedPluginCallbacks(LuaCallbackEngine   *engine,
                                                     const QSet<QString> &callbackNames) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::SetObservedPluginCallbacks,
	                         [&] { m_direct.setObservedPluginCallbacks(engine, callbackNames); });
}

bool LuaExecutorThreaded::hasObservedPluginCallback(LuaCallbackEngine *engine,
                                                    const QString     &functionName) const
{
	return invokeBlockingPolicyBool(
	    LuaExecutorOperation::HasObservedPluginCallback,
	    [&] { return m_direct.hasObservedPluginCallback(engine, functionName); }, false);
}

void LuaExecutorThreaded::setCallbackCatalogObserver(LuaCallbackEngine    *engine,
                                                     std::function<void()> observer) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::SetCallbackCatalogObserver,
	                         [&, movedObserver = std::move(observer)]() mutable
	                         { m_direct.setCallbackCatalogObserver(engine, std::move(movedObserver)); });
}

bool LuaExecutorThreaded::loadScript(LuaCallbackEngine *engine) const
{
	return invokeBlockingPolicyBool(
	    LuaExecutorOperation::LoadScript, [&] { return m_direct.loadScript(engine); }, false);
}

bool LuaExecutorThreaded::hasFunction(LuaCallbackEngine *engine, const QString &functionName) const
{
	return invokeBlockingPolicyBool(
	    LuaExecutorOperation::HasFunction, [&] { return m_direct.hasFunction(engine, functionName); }, false);
}

bool LuaExecutorThreaded::callFunctionNoArgs(LuaCallbackEngine *engine, const QString &functionName,
                                             bool *hasFunction, const bool defaultResult) const
{
	bool       localHasFunction = false;
	const bool result           = invokeBlockingPolicyBool(
        LuaExecutorOperation::CallFunctionNoArgs,
        [&]
        {
            return m_direct.callFunctionNoArgs(engine, functionName,
                                               hasFunction ? &localHasFunction : nullptr, defaultResult);
        },
        defaultResult);
	if (hasFunction)
		*hasFunction = localHasFunction;
	return result;
}

bool LuaExecutorThreaded::callFunctionWithString(LuaCallbackEngine *engine, const QString &functionName,
                                                 const QString &arg, bool *hasFunction,
                                                 const bool defaultResult) const
{
	bool       localHasFunction = false;
	const bool result           = invokeBlockingPolicyBool(
        LuaExecutorOperation::CallFunctionWithString,
        [&]
        {
            return m_direct.callFunctionWithString(engine, functionName, arg,
                                                   hasFunction ? &localHasFunction : nullptr, defaultResult);
        },
        defaultResult);
	if (hasFunction)
		*hasFunction = localHasFunction;
	return result;
}

bool LuaExecutorThreaded::callFunctionWithBytes(LuaCallbackEngine *engine, const QString &functionName,
                                                const QByteArray &arg, bool *hasFunction,
                                                const bool defaultResult) const
{
	bool       localHasFunction = false;
	const bool result           = invokeBlockingPolicyBool(
        LuaExecutorOperation::CallFunctionWithBytes,
        [&]
        {
            return m_direct.callFunctionWithBytes(engine, functionName, arg,
                                                  hasFunction ? &localHasFunction : nullptr, defaultResult);
        },
        defaultResult);
	if (hasFunction)
		*hasFunction = localHasFunction;
	return result;
}

bool LuaExecutorThreaded::callFunctionWithBytesInOut(LuaCallbackEngine *engine, const QString &functionName,
                                                     QByteArray &arg, bool *hasFunction) const
{
	QByteArray localArg         = arg;
	bool       localHasFunction = false;
	const bool result           = invokeBlockingPolicyBool(
        LuaExecutorOperation::CallFunctionWithBytesInOut,
        [&]
        {
            return m_direct.callFunctionWithBytesInOut(engine, functionName, localArg,
                                                       hasFunction ? &localHasFunction : nullptr);
        },
        true);
	arg = localArg;
	if (hasFunction)
		*hasFunction = localHasFunction;
	return result;
}

bool LuaExecutorThreaded::callFunctionWithStringInOut(LuaCallbackEngine *engine, const QString &functionName,
                                                      QString &arg, bool *hasFunction) const
{
	QString    localArg         = arg;
	bool       localHasFunction = false;
	const bool result           = invokeBlockingPolicyBool(
        LuaExecutorOperation::CallFunctionWithStringInOut,
        [&]
        {
            return m_direct.callFunctionWithStringInOut(engine, functionName, localArg,
                                                        hasFunction ? &localHasFunction : nullptr);
        },
        true);
	arg = localArg;
	if (hasFunction)
		*hasFunction = localHasFunction;
	return result;
}

bool LuaExecutorThreaded::callFunctionWithNumberAndString(LuaCallbackEngine *engine,
                                                          const QString &functionName, const long arg1,
                                                          const QString &arg2, bool *hasFunction,
                                                          const bool defaultResult) const
{
	bool       localHasFunction = false;
	const bool result           = invokeBlockingPolicyBool(
        LuaExecutorOperation::CallFunctionWithNumberAndString,
        [&]
        {
            return m_direct.callFunctionWithNumberAndString(
                engine, functionName, arg1, arg2, hasFunction ? &localHasFunction : nullptr, defaultResult);
        },
        defaultResult);
	if (hasFunction)
		*hasFunction = localHasFunction;
	return result;
}

bool LuaExecutorThreaded::callFunctionWithTwoNumbersAndString(LuaCallbackEngine *engine,
                                                              const QString &functionName, const long arg1,
                                                              const long arg2, const QString &arg3,
                                                              bool      *hasFunction,
                                                              const bool defaultResult) const
{
	bool       localHasFunction = false;
	const bool result           = invokeBlockingPolicyBool(
        LuaExecutorOperation::CallFunctionWithTwoNumbersAndString,
        [&]
        {
            return m_direct.callFunctionWithTwoNumbersAndString(engine, functionName, arg1, arg2, arg3,
                                                                hasFunction ? &localHasFunction : nullptr,
		                                                                  defaultResult);
        },
        defaultResult);
	if (hasFunction)
		*hasFunction = localHasFunction;
	return result;
}

bool LuaExecutorThreaded::callFunctionWithNumberAndBytes(LuaCallbackEngine *engine,
                                                         const QString &functionName, const long arg1,
                                                         const QByteArray &arg2, bool *hasFunction,
                                                         const bool defaultResult) const
{
	bool       localHasFunction = false;
	const bool result           = invokeBlockingPolicyBool(
        LuaExecutorOperation::CallFunctionWithNumberAndBytes,
        [&]
        {
            return m_direct.callFunctionWithNumberAndBytes(
                engine, functionName, arg1, arg2, hasFunction ? &localHasFunction : nullptr, defaultResult);
        },
        defaultResult);
	if (hasFunction)
		*hasFunction = localHasFunction;
	return result;
}

bool LuaExecutorThreaded::callFunctionWithNumberAndUtf8Strings(
    LuaCallbackEngine *engine, const QString &functionName, const long arg1, const QByteArray &arg2Utf8,
    const QByteArray &arg3Utf8, const QByteArray &arg4Utf8, bool *hasFunction, const bool defaultResult) const
{
	bool       localHasFunction = false;
	const bool result           = invokeBlockingPolicyBool(
        LuaExecutorOperation::CallFunctionWithNumberAndUtf8Strings,
        [&]
        {
            return m_direct.callFunctionWithNumberAndUtf8Strings(
                engine, functionName, arg1, arg2Utf8, arg3Utf8, arg4Utf8,
                hasFunction ? &localHasFunction : nullptr, defaultResult);
        },
        defaultResult);
	if (hasFunction)
		*hasFunction = localHasFunction;
	return result;
}

bool LuaExecutorThreaded::callProcedureWithString(LuaCallbackEngine *engine, const QString &functionName,
                                                  const QString &arg, bool *hasFunction) const
{
	bool       localHasFunction = false;
	const bool result           = invokeBlockingPolicyBool(
        LuaExecutorOperation::CallProcedureWithString,
        [&]
        {
            return m_direct.callProcedureWithString(engine, functionName, arg,
                                                    hasFunction ? &localHasFunction : nullptr);
        },
        false);
	if (hasFunction)
		*hasFunction = localHasFunction;
	return result;
}

bool LuaExecutorThreaded::callMxpError(LuaCallbackEngine *engine, const QString &functionName,
                                       const int level, const long messageNumber, const int lineNumber,
                                       const QString &message) const
{
	return invokeBlockingPolicyBool(
	    LuaExecutorOperation::CallMxpError, [&]
	    { return m_direct.callMxpError(engine, functionName, level, messageNumber, lineNumber, message); },
	    false);
}

void LuaExecutorThreaded::callMxpStartUp(LuaCallbackEngine *engine, const QString &functionName) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::CallMxpStartUp,
	                         [&] { m_direct.callMxpStartUp(engine, functionName); });
}

void LuaExecutorThreaded::callMxpShutDown(LuaCallbackEngine *engine, const QString &functionName) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::CallMxpShutDown,
	                         [&] { m_direct.callMxpShutDown(engine, functionName); });
}

bool LuaExecutorThreaded::callMxpStartTag(LuaCallbackEngine *engine, const QString &functionName,
                                          const QString &name, const QString &args,
                                          const QMap<QString, QString> &table) const
{
	return invokeBlockingPolicyBool(
	    LuaExecutorOperation::CallMxpStartTag,
	    [&] { return m_direct.callMxpStartTag(engine, functionName, name, args, table); }, false);
}

void LuaExecutorThreaded::callMxpEndTag(LuaCallbackEngine *engine, const QString &functionName,
                                        const QString &name, const QString &text) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::CallMxpEndTag,
	                         [&] { m_direct.callMxpEndTag(engine, functionName, name, text); });
}

void LuaExecutorThreaded::callMxpSetVariable(LuaCallbackEngine *engine, const QString &functionName,
                                             const QString &name, const QString &contents) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::CallMxpSetVariable,
	                         [&] { m_direct.callMxpSetVariable(engine, functionName, name, contents); });
}

bool LuaExecutorThreaded::callFunctionWithStringsAndWildcards(
    LuaCallbackEngine *engine, const QString &functionName, const QStringList &args,
    const QStringList &wildcards, const QMap<QString, QString> &namedWildcards,
    const QVector<LuaStyleRun> *styleRuns, bool *hasFunction) const
{
	const QVector<LuaStyleRun>  styleRunsCopy    = styleRuns ? *styleRuns : QVector<LuaStyleRun>{};
	const QVector<LuaStyleRun> *styleRunsArg     = styleRuns ? &styleRunsCopy : nullptr;
	bool                        localHasFunction = false;
	const bool                  result           = invokeBlockingPolicyBool(
        LuaExecutorOperation::CallFunctionWithStringsAndWildcards,
        [&]
        {
            return m_direct.callFunctionWithStringsAndWildcards(engine, functionName, args, wildcards,
		                                                                                   namedWildcards, styleRunsArg,
                                                                hasFunction ? &localHasFunction : nullptr);
        },
        false);
	if (hasFunction)
		*hasFunction = localHasFunction;
	return result;
}

bool LuaExecutorThreaded::executeScript(LuaCallbackEngine *engine, const QString &code,
                                        const QString              &description,
                                        const QVector<LuaStyleRun> *styleRuns) const
{
	const QVector<LuaStyleRun>  styleRunsCopy = styleRuns ? *styleRuns : QVector<LuaStyleRun>{};
	const QVector<LuaStyleRun> *styleRunsArg  = styleRuns ? &styleRunsCopy : nullptr;
	return invokeBlockingPolicyBool(
	    LuaExecutorOperation::ExecuteScript,
	    [&] { return m_direct.executeScript(engine, code, description, styleRunsArg); }, false);
}

void LuaExecutorThreaded::refreshLuaCallbackCatalogNow(LuaCallbackEngine *engine) const
{
	invokeBlockingPolicyVoid(LuaExecutorOperation::RefreshLuaCallbackCatalogNow,
	                         [&] { m_direct.refreshLuaCallbackCatalogNow(engine); });
}

#ifdef QMUD_ENABLE_LUA_SCRIPTING
lua_State *LuaExecutorThreaded::luaState(LuaCallbackEngine *engine) const
{
	return invokeBlockingPolicyLuaState([&] { return m_direct.luaState(engine); });
}
#endif

void LuaExecutorThreaded::invokeBlockingVoid(const std::function<void()> &fn) const
{
	::invokeBlockingVoid(m_workerInvoker.get(), m_workerThread.get(), fn);
}

bool LuaExecutorThreaded::invokeBlockingBool(const std::function<bool()> &fn, const bool fallback) const
{
	return invokeBlockingResult<bool>(m_workerInvoker.get(), m_workerThread.get(), fn, fallback);
}

void LuaExecutorThreaded::warnIfPolicyNotWorkerLocal(const LuaExecutorOperation operation)
{
	Q_UNUSED(operation);
}

void LuaExecutorThreaded::invokeBlockingPolicyVoid(const LuaExecutorOperation   operation,
                                                   const std::function<void()> &fn) const
{
	warnIfPolicyNotWorkerLocal(operation);
	invokeBlockingVoid(fn);
}

bool LuaExecutorThreaded::invokeBlockingPolicyBool(const LuaExecutorOperation   operation,
                                                   const std::function<bool()> &fn, const bool fallback) const
{
	warnIfPolicyNotWorkerLocal(operation);
	return invokeBlockingBool(fn, fallback);
}

#ifdef QMUD_ENABLE_LUA_SCRIPTING
lua_State *LuaExecutorThreaded::invokeBlockingLuaState(const std::function<lua_State *()> &fn) const
{
	return invokeBlockingResult<lua_State *>(m_workerInvoker.get(), m_workerThread.get(), fn, nullptr);
}

lua_State *LuaExecutorThreaded::invokeBlockingPolicyLuaState(const std::function<lua_State *()> &fn) const
{
	warnIfPolicyNotWorkerLocal(LuaExecutorOperation::LuaState);
	return invokeBlockingLuaState(fn);
}
#endif

void LuaExecutorThreaded::shutdownWorker()
{
	if (m_workerInvoker)
	{
		QObject *workerObject = m_workerInvoker.release();
		QThread *ownerThread  = workerObject ? workerObject->thread() : nullptr;
		if (workerObject &&
		    (!ownerThread || ownerThread == QThread::currentThread() || !ownerThread->isRunning()))
		{
			delete workerObject;
		}
		else if (workerObject)
		{
			workerObject->deleteLater();
		}
	}

	if (m_workerThread && m_workerThread->isRunning())
	{
		m_workerThread->quit();
		if (QThread::currentThread() != m_workerThread.get())
			m_workerThread->wait();
	}
}
