/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: LuaExecutorWorker.cpp
 * Role: Worker-thread Lua executor backend implementation.
 */

#include "LuaExecutorWorker.h"

#include "LuaCallbackEngine.h"
#include "helpers/LuaExecutionUtils.h"

#include <QDeadlineTimer>
#include <QDebug>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <QWaitCondition>

#include <exception>
#include <new>

namespace
{
	const QString kWorkerDispatchFailurePrefix = QStringLiteral("[QMud][LuaExecutor] worker dispatch failed");

	LuaBatchDispatchResult fallbackBatchDispatchResult(const LuaBatchDispatchRequest &request)
	{
		LuaBatchDispatchResult result;
		result.stringResult = request.stringArg;
		result.bytesResult  = request.bytesArg;
		switch (request.kind)
		{
		case LuaBatchDispatchKind::HasFunction:
			result.hasFunction      = false;
			result.hasFunctionValid = true;
			break;
		case LuaBatchDispatchKind::ResetAndLoadScript:
			result.boolResult      = false;
			result.boolResultValid = true;
			break;
		case LuaBatchDispatchKind::InitializeEnginesWithObservedCallbacksMany:
		case LuaBatchDispatchKind::UpdateObservedCallbacksMany:
			break;
		case LuaBatchDispatchKind::CallPluginLuaMarshalling:
			result.boolResult            = false;
			result.boolResultValid       = true;
			result.marshallingError      = static_cast<int>(CallPluginLuaMarshallingError::NoSuchRoutine);
			result.marshallingErrorValid = true;
			result.marshallingIndex      = 0;
			result.marshallingTypeName   = {};
			result.marshallingRuntimeError.clear();
			result.marshallingReturnCount = 0;
			result.marshallingSameState   = false;
			break;
		case LuaBatchDispatchKind::StringStopOnFalse:
		case LuaBatchDispatchKind::NumberAndStringStopOnFalse:
		case LuaBatchDispatchKind::TwoNumbersAndStringStopOnFalse:
			result.boolResult      = true;
			result.boolResultValid = true;
			break;
		case LuaBatchDispatchKind::StringHandled:
		case LuaBatchDispatchKind::NumberAndStringStopOnTrue:
		case LuaBatchDispatchKind::NumberAndBytesStopOnTrue:
		case LuaBatchDispatchKind::MxpError:
		case LuaBatchDispatchKind::MxpStartTag:
		case LuaBatchDispatchKind::ExecuteScript:
			result.boolResult      = false;
			result.boolResultValid = true;
			break;
		case LuaBatchDispatchKind::NumberAndUtf8StringsCount:
			result.countResult      = 0;
			result.countResultValid = true;
			break;
		case LuaBatchDispatchKind::ProcedureWithString:
			result.boolResult       = false;
			result.boolResultValid  = true;
			result.hasFunction      = false;
			result.hasFunctionValid = true;
			break;
		default:
			break;
		}
		return result;
	}

	QString dispatchRequestDebugLabel(const LuaBatchDispatchRequest &request)
	{
		const QString callbackName = request.functionName.trimmed().isEmpty()
		                                 ? QStringLiteral("<none>")
		                                 : request.functionName.trimmed();
		return QStringLiteral("kind=%1 callback=%2 engines=%3")
		    .arg(static_cast<int>(request.kind))
		    .arg(callbackName)
		    .arg(request.engines.size());
	}

	bool ensureWorkerReady(const QObject *invoker, QThread *workerThread, std::atomic_bool *workerBridgeReady)
	{
		if (!invoker || !workerThread)
			return false;
		if (workerBridgeReady && workerBridgeReady->load(std::memory_order_acquire) &&
		    workerThread->isRunning())
		{
			return true;
		}
		if (!workerThread->isRunning())
			workerThread->start();
		if (!workerThread->isRunning())
		{
			if (workerBridgeReady)
				workerBridgeReady->store(false, std::memory_order_release);
			return false;
		}
		if (qmudLuaBridgeEnsureObjectThreadReady(invoker))
		{
			if (workerBridgeReady)
				workerBridgeReady->store(true, std::memory_order_release);
			return true;
		}
		if (workerBridgeReady)
			workerBridgeReady->store(false, std::memory_order_release);
		qWarning().noquote() << QStringLiteral("[QMud][LuaExecutor] worker bridge readiness failed: %1")
		                            .arg(qmudLuaBridgeLastError());
		return false;
	}

} // namespace

struct LuaExecutorWorker::QueuedDispatchRequest
{
		LuaBatchDispatchRequest                             request;
		LuaBatchDispatchResult                              fallback;
		bool                                                needsResult{false};
		QPointer<QObject>                                   completionTarget;
		std::function<void(const LuaBatchDispatchResult &)> completion;
		QPointer<QThread>                                   waiterThread;
		bool                                                started{false};
		bool                                                canceled{false};
		bool                                                completed{false};
		LuaBatchDispatchResult                              result;
		QMutex                                              mutex;
		QWaitCondition                                      wake;
};

LuaExecutorWorker::LuaExecutorWorker()
{
	m_controlLane.name  = "control";
	m_callbackLane.name = "callback";
	initializeWorkerLane(m_controlLane, QStringLiteral("QMudLuaExecutorControlWorker"));
	initializeWorkerLane(m_callbackLane, QStringLiteral("QMudLuaExecutorCallbackWorker"));
}

LuaExecutorWorker::~LuaExecutorWorker()
{
	shutdownWorker();
}

const char *LuaExecutorWorker::laneName(const WorkerLaneState &lane)
{
	return lane.name ? lane.name : "unknown";
}

LuaExecutorWorker::WorkerLaneState &
LuaExecutorWorker::laneStateForRequest(const LuaBatchDispatchRequest &request) const
{
	if (request.lane == LuaBatchDispatchLane::Control)
	{
		static std::atomic_bool warnedUnsupportedControlLane{false};
		if (!warnedUnsupportedControlLane.exchange(true, std::memory_order_acq_rel))
		{
			qWarning().noquote() << QStringLiteral(
			                            "[QMud][LuaExecutor] Control lane requested for Lua-state dispatch "
			                            "kind=%1; forcing callback lane to preserve serialization")
			                            .arg(static_cast<int>(request.kind));
		}
	}
	return m_callbackLane;
}

void LuaExecutorWorker::initializeWorkerLane(WorkerLaneState &lane, const QString &threadName)
{
	lane.thread = std::make_unique<QThread>();
	lane.thread->setObjectName(threadName);
	lane.invoker = std::make_unique<QObject>();
	lane.invoker->moveToThread(lane.thread.get());
	lane.thread->start();
	if (!ensureWorkerReady(lane.invoker.get(), lane.thread.get(), &lane.bridgeReady))
	{
		qWarning().noquote() << QStringLiteral("[QMud][LuaExecutor] %1 worker thread failed to start")
		                            .arg(QString::fromUtf8(laneName(lane)));
	}
}

LuaBatchDispatchResult LuaExecutorWorker::dispatchBatch(const LuaBatchDispatchRequest &request) const
{
	WorkerLaneState       &lane     = laneStateForRequest(request);
	LuaBatchDispatchResult fallback = fallbackBatchDispatchResult(request);
	const auto             cancelQueuedBeforeStart =
	    [&lane](const QSharedPointer<QueuedDispatchRequest> &queuedRequest) -> bool
	{
		if (!queuedRequest)
			return false;
		{
			QMutexLocker queueLocker(&lane.dispatchQueueMutex);
			static_cast<void>(lane.dispatchQueue.removeOne(queuedRequest));
		}
		bool canceled = false;
		{
			QMutexLocker locker(&queuedRequest->mutex);
			if (queuedRequest->completed || queuedRequest->started)
				return false;
			queuedRequest->canceled  = true;
			queuedRequest->completed = true;
			queuedRequest->result    = queuedRequest->fallback;
			queuedRequest->wake.wakeAll();
			canceled = true;
		}
		return canceled;
	};

	if (!lane.thread)
		return fallback;
	if (QThread::currentThread() == lane.thread.get())
		return m_direct.dispatchBatch(request);
	if (!lane.invoker || !ensureWorkerReady(lane.invoker.get(), lane.thread.get(), &lane.bridgeReady))
		return fallback;
	if (m_workerShuttingDown.load(std::memory_order_acquire))
		return fallback;
	const bool     bridgeReentrantCaller = qmudLuaBridgeIsExecutingRequestOnCurrentThread();
	const bool     useCallbackLane       = &lane == &m_callbackLane;
	QObject *const laneInvoker = useCallbackLane ? m_callbackLane.invoker.get() : m_controlLane.invoker.get();
	bool           bridgeDriveFailed             = false;
	const auto     driveLaneOnceForBridgeReentry = [&]() -> bool
	{
		if (!bridgeReentrantCaller || bridgeDriveFailed || !laneInvoker)
			return false;
		bool       ranWork = false;
		const bool bridged =
		    qmudLuaBridgeInvokeOnObjectThread(laneInvoker,
		                                      [this, useCallbackLane, &ranWork]
		                                      {
			                                      WorkerLaneState &targetLane =
			                                          useCallbackLane ? m_callbackLane : m_controlLane;
			                                      ranWork = processOneQueuedDispatch(targetLane);
		                                      });
		if (bridged)
			return ranWork;
		bridgeDriveFailed = true;
		qWarning().noquote() << QStringLiteral("%1: %2-lane bridge re-entry queue drive failed: %3")
		                            .arg(kWorkerDispatchFailurePrefix, QString::fromUtf8(laneName(lane)),
		                                 qmudLuaBridgeLastError());
		return false;
	};

	const auto queuedRequest    = QSharedPointer<QueuedDispatchRequest>::create();
	queuedRequest->request      = request;
	queuedRequest->fallback     = fallback;
	queuedRequest->needsResult  = true;
	queuedRequest->waiterThread = QThread::currentThread();
	if (!enqueueQueuedDispatch(lane, queuedRequest))
		return queuedRequest->fallback;

	const qint64   invokeTimeoutMs = qMax<qint64>(1, qmudLuaBridgeInvokeTimeoutMs());
	QDeadlineTimer deadline(invokeTimeoutMs);
	for (;;)
	{
		{
			QMutexLocker locker(&queuedRequest->mutex);
			if (queuedRequest->completed)
				return queuedRequest->result;
		}
		if (!lane.thread || !lane.thread->isRunning())
		{
			if (cancelQueuedBeforeStart(queuedRequest))
			{
				qWarning().noquote() << QStringLiteral(
				                            "%1: canceled queued %2-lane worker dispatch because lane thread "
				                            "stopped before execution")
				                            .arg(kWorkerDispatchFailurePrefix,
				                                 QString::fromUtf8(laneName(lane)));
				return queuedRequest->fallback;
			}
			break;
		}
		if (deadline.hasExpired())
			break;
		if (qmudLuaBridgePumpCurrentThreadOnce())
			continue;
		if (driveLaneOnceForBridgeReentry())
			continue;
		{
			QMutexLocker locker(&queuedRequest->mutex);
			if (queuedRequest->completed)
				return queuedRequest->result;
		}

		const qint64 remainingMs  = qMax<qint64>(1, deadline.remainingTime());
		const int    bridgeWaitMs = static_cast<int>(qMin<qint64>(remainingMs, 100));
		static_cast<void>(qmudLuaBridgeWaitForCurrentThreadWake(bridgeWaitMs));
	}
	{
		QMutexLocker locker(&queuedRequest->mutex);
		if (queuedRequest->completed)
			return queuedRequest->result;
	}
	qWarning().noquote() << QStringLiteral("%1: queued %2-lane worker dispatch exceeded %3 ms; waiting "
	                                       "for completion (%4)")
	                            .arg(kWorkerDispatchFailurePrefix, QString::fromUtf8(laneName(lane)))
	                            .arg(invokeTimeoutMs)
	                            .arg(dispatchRequestDebugLabel(request));
	for (;;)
	{
		{
			QMutexLocker locker(&queuedRequest->mutex);
			if (queuedRequest->completed)
				return queuedRequest->result;
		}
		if (m_workerShuttingDown.load(std::memory_order_acquire) || !lane.thread || !lane.thread->isRunning())
		{
			const bool canceled = cancelQueuedBeforeStart(queuedRequest);
			qWarning().noquote()
			    << QStringLiteral("%1: %2-lane worker stopped while waiting for queued dispatch completion")
			           .arg(kWorkerDispatchFailurePrefix, QString::fromUtf8(laneName(lane)));
			Q_UNUSED(canceled);
			return queuedRequest->fallback;
		}
		if (qmudLuaBridgePumpCurrentThreadOnce())
			continue;
		if (driveLaneOnceForBridgeReentry())
			continue;
		static_cast<void>(qmudLuaBridgeWaitForCurrentThreadWake(100));
	}
	Q_UNREACHABLE_RETURN(queuedRequest->fallback);
}

void LuaExecutorWorker::dispatchBatchAsync(const LuaBatchDispatchRequest &request) const
{
	dispatchBatchAsync(request, nullptr, {});
}

void LuaExecutorWorker::dispatchBatchAsync(
    const LuaBatchDispatchRequest &request, QObject *completionTarget,
    const std::function<void(const LuaBatchDispatchResult &)> &completion) const
{
	WorkerLaneState       &lane     = laneStateForRequest(request);
	LuaBatchDispatchResult fallback = fallbackBatchDispatchResult(request);
	const auto deliverCompletion = [](const QPointer<QObject>                                   &target,
	                                  const std::function<void(const LuaBatchDispatchResult &)> &completionFn,
	                                  const LuaBatchDispatchResult                              &result)
	{
		if (!completionFn)
			return;
		if (target && target->thread() != QThread::currentThread())
		{
			auto       queuedCompletion = completionFn;
			const bool queued           = QMetaObject::invokeMethod(
			    target.data(),
			    [target, completion = std::move(queuedCompletion), result]() mutable
			    {
				    if (!target)
					    return;
				    completion(result);
			    },
			    Qt::QueuedConnection);
			if (!queued)
			{
				qWarning().noquote() << QStringLiteral(
				                            "%1: failed to queue completion delivery to target thread")
				                            .arg(kWorkerDispatchFailurePrefix);
			}
			return;
		}
		completionFn(result);
	};

	if (!lane.thread)
	{
		deliverCompletion(completionTarget, completion, fallback);
		return;
	}
	if (QThread::currentThread() == lane.thread.get())
	{
		const LuaBatchDispatchResult result = m_direct.dispatchBatch(request);
		deliverCompletion(completionTarget, completion, result);
		return;
	}
	if (!lane.invoker || !ensureWorkerReady(lane.invoker.get(), lane.thread.get(), &lane.bridgeReady))
	{
		deliverCompletion(completionTarget, completion, fallback);
		return;
	}
	if (m_workerShuttingDown.load(std::memory_order_acquire))
	{
		deliverCompletion(completionTarget, completion, fallback);
		return;
	}

	const auto queuedRequest        = QSharedPointer<QueuedDispatchRequest>::create();
	queuedRequest->request          = request;
	queuedRequest->fallback         = fallback;
	queuedRequest->needsResult      = false;
	queuedRequest->completionTarget = completionTarget;
	queuedRequest->completion       = completion;
	if (!enqueueQueuedDispatch(lane, queuedRequest))
		deliverCompletion(queuedRequest->completionTarget, queuedRequest->completion,
		                  queuedRequest->fallback);
}

bool LuaExecutorWorker::enqueueQueuedDispatch(WorkerLaneState                             &lane,
                                              const QSharedPointer<QueuedDispatchRequest> &request) const
{
	if (!request || !lane.invoker)
		return false;
	bool schedulePump = false;
	{
		QMutexLocker locker(&lane.dispatchQueueMutex);
		if (m_workerShuttingDown.load(std::memory_order_acquire))
			return false;
		lane.dispatchQueue.enqueue(request);
		if (!lane.dispatchQueuePumpQueued)
		{
			lane.dispatchQueuePumpQueued = true;
			schedulePump                 = true;
		}
	}
	if (lane.thread)
		qmudLuaBridgeNotifyThreadWake(lane.thread.get());
	if (!schedulePump)
		return true;
	const bool     useCallbackLane = &lane == &m_callbackLane;
	QObject *const laneInvoker = useCallbackLane ? m_callbackLane.invoker.get() : m_controlLane.invoker.get();
	if (!laneInvoker)
		return false;
	const bool queued = QMetaObject::invokeMethod(
	    laneInvoker,
	    [this, useCallbackLane]
	    {
		    WorkerLaneState &targetLane = useCallbackLane ? m_callbackLane : m_controlLane;
		    processQueuedDispatches(targetLane);
	    },
	    Qt::QueuedConnection);
	if (queued)
		return true;

	lane.bridgeReady.store(false, std::memory_order_release);
	qWarning().noquote() << QStringLiteral("%1: failed to queue %2-lane worker dispatch pump")
	                            .arg(kWorkerDispatchFailurePrefix, QString::fromUtf8(laneName(lane)));

	QMutexLocker locker(&lane.dispatchQueueMutex);
	lane.dispatchQueuePumpQueued = false;
	const bool removed           = lane.dispatchQueue.removeOne(request);
	if (!removed)
		return false;
	if (!request->needsResult)
		return false;
	QMutexLocker requestLocker(&request->mutex);
	request->completed = true;
	request->result    = request->fallback;
	request->wake.wakeAll();
	qmudLuaBridgeNotifyThreadWake(request->waiterThread.data());
	return false;
}

bool LuaExecutorWorker::processOneQueuedDispatch(WorkerLaneState &lane) const
{
	if (!lane.thread || QThread::currentThread() != lane.thread.get())
		return false;

	QSharedPointer<QueuedDispatchRequest> request;
	{
		QMutexLocker locker(&lane.dispatchQueueMutex);
		if (lane.dispatchQueue.isEmpty())
		{
			lane.dispatchQueuePumpQueued = false;
			return false;
		}
		request = lane.dispatchQueue.dequeue();
	}

	if (!request)
		return true;
	{
		QMutexLocker requestLocker(&request->mutex);
		if (request->completed || request->canceled)
		{
			if (request->needsResult && !request->completed)
			{
				request->completed = true;
				request->result    = request->fallback;
				request->wake.wakeAll();
			}
			return true;
		}
		request->started = true;
	}

	LuaBatchDispatchResult result = request->fallback;
	try
	{
		result = m_direct.dispatchBatch(request->request);
	}
	catch (const std::bad_alloc &)
	{
		qWarning().noquote() << QStringLiteral("%1: std::bad_alloc in queued worker dispatch")
		                            .arg(kWorkerDispatchFailurePrefix);
	}
	catch (const std::exception &ex)
	{
		qWarning().noquote() << QStringLiteral("%1: exception in queued worker dispatch: %2")
		                            .arg(kWorkerDispatchFailurePrefix, QString::fromLocal8Bit(ex.what()));
	}
	catch (...)
	{
		qWarning().noquote() << QStringLiteral("%1: unknown exception in queued worker dispatch")
		                            .arg(kWorkerDispatchFailurePrefix);
	}
	if (request->completion)
	{
		auto       completion = std::move(request->completion);
		const auto target     = request->completionTarget;
		if (target && target->thread() != QThread::currentThread())
		{
			auto       queuedCompletion = completion;
			const bool queued           = QMetaObject::invokeMethod(
			    target.data(),
			    [target, completion = std::move(queuedCompletion), result]() mutable
			    {
				    if (!target)
					    return;
				    completion(result);
			    },
			    Qt::QueuedConnection);
			if (!queued)
			{
				qWarning().noquote() << QStringLiteral(
				                            "%1: failed to queue completion delivery to target thread")
				                            .arg(kWorkerDispatchFailurePrefix);
			}
		}
		else
		{
			completion(result);
		}
	}
	if (!request->needsResult)
		return true;
	{
		QMutexLocker requestLocker(&request->mutex);
		if (!request->completed)
		{
			request->result    = result;
			request->completed = true;
			request->wake.wakeAll();
		}
	}
	qmudLuaBridgeNotifyThreadWake(request->waiterThread.data());
	return true;
}

void LuaExecutorWorker::processQueuedDispatches(WorkerLaneState &lane) const
{
	while (processOneQueuedDispatch(lane))
	{
	}
}

void LuaExecutorWorker::shutdownWorker() const
{
	m_workerShuttingDown.store(true, std::memory_order_release);
	shutdownWorkerLane(m_controlLane);
	shutdownWorkerLane(m_callbackLane);
}

void LuaExecutorWorker::shutdownWorkerLane(WorkerLaneState &lane)
{
	QVector<QSharedPointer<QueuedDispatchRequest>> pendingRequests;
	{
		QMutexLocker locker(&lane.dispatchQueueMutex);
		pendingRequests.reserve(lane.dispatchQueue.size());
		while (!lane.dispatchQueue.isEmpty())
		{
			pendingRequests.push_back(lane.dispatchQueue.dequeue());
		}
		lane.dispatchQueuePumpQueued = false;
	}
	for (const QSharedPointer<QueuedDispatchRequest> &request : pendingRequests)
	{
		if (!request)
			continue;
		if (request->completion)
		{
			auto       completion = std::move(request->completion);
			const auto target     = request->completionTarget;
			if (target && target->thread() != QThread::currentThread())
			{
				auto       queuedCompletion = completion;
				const bool queued           = QMetaObject::invokeMethod(
				    target.data(),
				    [target, completion = std::move(queuedCompletion), request]() mutable
				    {
					    if (!target)
						    return;
					    completion(request->fallback);
				    },
				    Qt::QueuedConnection);
				if (!queued)
				{
					qWarning().noquote()
					    << QStringLiteral("%1: failed to queue completion delivery to target thread")
					           .arg(kWorkerDispatchFailurePrefix);
				}
			}
			else
			{
				completion(request->fallback);
			}
		}
		if (!request->needsResult)
			continue;
		QMutexLocker requestLocker(&request->mutex);
		request->completed = true;
		request->result    = request->fallback;
		request->wake.wakeAll();
		qmudLuaBridgeNotifyThreadWake(request->waiterThread.data());
	}
	if (lane.invoker)
	{
		QObject *workerObject = lane.invoker.release();
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

	if (lane.thread && lane.thread->isRunning())
	{
		lane.thread->quit();
		if (QThread::currentThread() != lane.thread.get())
			lane.thread->wait();
	}
	lane.bridgeReady.store(false, std::memory_order_release);
}
