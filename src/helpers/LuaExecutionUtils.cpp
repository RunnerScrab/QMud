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
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDebug>
#include <QEvent>
#include <QHash>
#include <QList>
// ReSharper disable once CppUnusedIncludeDirective
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
// ReSharper disable once CppUnusedIncludeDirective
#include <QStringList>
#include <QThread>
#include <QWaitCondition>
#include <QtGlobal>

#include <memory>

namespace
{
	constexpr qint64 kLuaBridgeReadyTimeoutMs  = 5000;
	constexpr qint64 kLuaBridgeInvokeTimeoutMs = 30000;

	enum class LuaBridgeContextState
	{
		Running,
		Stopping,
		Stopped
	};

	struct LuaBridgeThreadContext;
	bool                 scheduleLuaBridgePump(const std::shared_ptr<LuaBridgeThreadContext> &context);

	thread_local QString t_luaBridgeLastError;
	thread_local LuaBridgeInvokeStatus t_luaBridgeLastStatus{LuaBridgeInvokeStatus::Success};

	QString                            threadLabel(const QThread *thread)
	{
		if (!thread)
			return QStringLiteral("<null>");
		const QString name = thread->objectName().trimmed();
		if (name.isEmpty())
			return QStringLiteral("0x%1").arg(
			    QString::number(reinterpret_cast<quintptr>(thread), 16).toUpper());
		return QStringLiteral("%1 [0x%2]")
		    .arg(name, QString::number(reinterpret_cast<quintptr>(thread), 16).toUpper());
	}

	bool setLuaBridgeError(const LuaBridgeInvokeStatus status, const QString &message)
	{
		t_luaBridgeLastStatus = status;
		t_luaBridgeLastError  = message;
		qWarning().noquote() << message;
		return false;
	}

	void clearLuaBridgeError()
	{
		t_luaBridgeLastStatus = LuaBridgeInvokeStatus::Success;
		t_luaBridgeLastError.clear();
	}

	QEvent::Type luaBridgePumpEventType()
	{
		static const auto type = static_cast<QEvent::Type>(QEvent::registerEventType());
		return type;
	}

	class LuaBridgePumpEvent final : public QEvent
	{
		public:
			LuaBridgePumpEvent() : QEvent(luaBridgePumpEventType())
			{
			}
	};

	struct LuaBridgeRequest
	{
			std::function<void()>                   fn;
			std::shared_ptr<LuaBridgeThreadContext> waiterContext;
			QMutex                                  mutex;
			QWaitCondition                          wake;
			bool                                    done{false};
			bool                                    canceled{false};
			LuaBridgeInvokeStatus                   cancelStatus{LuaBridgeInvokeStatus::UnknownFailure};
			QString                                 cancelReason;
	};

	struct LuaBridgeThreadContext
	{
			QMutex                                   mutex;
			QWaitCondition                           wake;
			QList<std::shared_ptr<LuaBridgeRequest>> queue;
			QPointer<QObject>                        pumpInvoker;
			bool                                     pumpPosted{false};
			QPointer<QThread>                        thread;
			LuaBridgeContextState                    state{LuaBridgeContextState::Stopped};
			quint64                                  wakeSerial{0};
	};

	class LuaBridgePumpInvoker final : public QObject
	{
		public:
			explicit LuaBridgePumpInvoker(const std::weak_ptr<LuaBridgeThreadContext> &context)
			    : m_context(context)
			{
			}
			bool event(QEvent *event) override
			{
				if (event->type() == luaBridgePumpEventType())
				{
					if (const auto context = m_context.lock())
					{
						for (;;)
						{
							std::shared_ptr<LuaBridgeRequest> request;
							{
								QMutexLocker locker(&context->mutex);
								if (context->queue.isEmpty())
								{
									context->pumpPosted = false;
									break;
								}
								request = context->queue.takeFirst();
							}
							if (!request)
								continue;
							if (request->fn)
								request->fn();
							{
								QMutexLocker requestLocker(&request->mutex);
								request->done = true;
								request->wake.wakeAll();
							}
							if (const auto waiterContext = request->waiterContext)
							{
								QMutexLocker waiterLocker(&waiterContext->mutex);
								++waiterContext->wakeSerial;
								waiterContext->wake.wakeAll();
							}
						}
					}
					return true;
				}
				return QObject::event(event);
			}

		private:
			std::weak_ptr<LuaBridgeThreadContext> m_context;
	};

	QMutex                                                    g_luaBridgeContextsMutex;
	QHash<QThread *, std::shared_ptr<LuaBridgeThreadContext>> g_luaBridgeContexts;

	void                                                      purgeDeadLuaBridgeContextsLocked()
	{
		auto it = g_luaBridgeContexts.begin();
		while (it != g_luaBridgeContexts.end())
		{
			const std::shared_ptr<LuaBridgeThreadContext> &ctx = it.value();
			if (!ctx || ctx->thread.isNull() || !ctx->thread->isRunning())
				it = g_luaBridgeContexts.erase(it);
			else
				++it;
		}
	}

	bool targetThreadRunnable(const QThread *thread, QString &reason, LuaBridgeInvokeStatus &failureStatus)
	{
		if (!thread)
		{
			reason        = QStringLiteral("target thread pointer is null");
			failureStatus = LuaBridgeInvokeStatus::MissingTargetBridgeContext;
			return false;
		}
		if (!thread->isRunning())
		{
			reason        = QStringLiteral("target thread is not running");
			failureStatus = LuaBridgeInvokeStatus::TargetThreadNotRunning;
			return false;
		}
		if (!thread->eventDispatcher())
		{
			reason        = QStringLiteral("target thread has no event dispatcher");
			failureStatus = LuaBridgeInvokeStatus::TargetThreadNoEventDispatcher;
			return false;
		}
		failureStatus = LuaBridgeInvokeStatus::Success;
		return true;
	}

	std::shared_ptr<LuaBridgeThreadContext> contextForThreadLocked(QThread *thread)
	{
		Q_ASSERT(thread);
		purgeDeadLuaBridgeContextsLocked();

		auto &ctx = g_luaBridgeContexts[thread];
		if (!ctx)
		{
			ctx         = std::make_shared<LuaBridgeThreadContext>();
			ctx->thread = thread;
		}
		return ctx;
	}

	std::shared_ptr<LuaBridgeThreadContext> contextForThread(QThread *thread)
	{
		if (!thread)
			return nullptr;
		QMutexLocker locker(&g_luaBridgeContextsMutex);
		return contextForThreadLocked(thread);
	}

	QObject *ensureLuaBridgePumpInvoker(const std::shared_ptr<LuaBridgeThreadContext> &context)
	{
		if (!context || context->thread.isNull())
			return nullptr;
		QMutexLocker locker(&context->mutex);
		if (context->pumpInvoker)
			return context->pumpInvoker.data();
		auto *const invoker = new LuaBridgePumpInvoker(context);
		invoker->moveToThread(context->thread.data());
		QObject::connect(
		    context->thread.data(), &QThread::finished, invoker,
		    [weakContext = std::weak_ptr<LuaBridgeThreadContext>(context)]
		    {
			    QList<std::shared_ptr<LuaBridgeRequest>> pendingRequests;
			    QObject                                 *invokerToDelete = nullptr;
			    const QString                            stopReason      = QStringLiteral(
                    "[QMud][LuaBridge] Invoke failed: target bridge thread stopped before request dispatch");
			    if (const auto locked = weakContext.lock())
			    {
				    QMutexLocker finishedLocker(&locked->mutex);
				    if (locked->state == LuaBridgeContextState::Running)
					    locked->state = LuaBridgeContextState::Stopping;
				    pendingRequests.swap(locked->queue);
				    invokerToDelete     = locked->pumpInvoker.data();
				    locked->state       = LuaBridgeContextState::Stopped;
				    locked->pumpInvoker = nullptr;
				    locked->pumpPosted  = false;
				    ++locked->wakeSerial;
				    locked->wake.wakeAll();
			    }
			    for (const std::shared_ptr<LuaBridgeRequest> &request : pendingRequests)
			    {
				    if (!request)
					    continue;
				    {
					    QMutexLocker requestLocker(&request->mutex);
					    if (!request->done)
					    {
						    request->canceled     = true;
						    request->cancelStatus = LuaBridgeInvokeStatus::RequestCanceledTargetThreadStopped;
						    request->cancelReason = stopReason;
						    request->done         = true;
					    }
					    request->wake.wakeAll();
				    }
				    if (const auto waiterContext = request->waiterContext)
				    {
					    QMutexLocker waiterLocker(&waiterContext->mutex);
					    ++waiterContext->wakeSerial;
					    waiterContext->wake.wakeAll();
				    }
			    }
			    if (invokerToDelete)
			    {
				    if (QThread::currentThread() == invokerToDelete->thread())
					    delete invokerToDelete;
				    else
					    invokerToDelete->deleteLater();
			    }
		    },
		    Qt::DirectConnection);
		context->pumpInvoker = invoker;
		return invoker;
	}

	QMutex                                              g_luaBridgeWaitGraphMutex;
	QHash<const QThread *, QHash<const QThread *, int>> g_luaBridgeWaitGraph;

	bool waitGraphEnter(const QThread *waiterThread, const QThread *targetThread)
	{
		Q_ASSERT(waiterThread);
		Q_ASSERT(targetThread);
		if (waiterThread == targetThread)
		{
			return setLuaBridgeError(LuaBridgeInvokeStatus::UnknownFailure,
			                         QStringLiteral("[QMud][LuaBridge] unexpected self-wait edge %1 -> %2")
			                             .arg(threadLabel(waiterThread), threadLabel(targetThread)));
		}
		QMutexLocker locker(&g_luaBridgeWaitGraphMutex);
		auto        &targetRefs  = g_luaBridgeWaitGraph[waiterThread];
		targetRefs[targetThread] = targetRefs.value(targetThread) + 1;
		return true;
	}

	void waitGraphLeave(const QThread *waiterThread, const QThread *targetThread)
	{
		Q_ASSERT(waiterThread);
		Q_ASSERT(targetThread);
		QMutexLocker locker(&g_luaBridgeWaitGraphMutex);
		auto         waiterIt = g_luaBridgeWaitGraph.find(waiterThread);
		if (waiterIt == g_luaBridgeWaitGraph.end())
			return;
		auto targetIt = waiterIt->find(targetThread);
		if (targetIt == waiterIt->end())
			return;
		if (targetIt.value() <= 1)
			waiterIt->erase(targetIt);
		else
			targetIt.value() -= 1;
		if (waiterIt->isEmpty())
			g_luaBridgeWaitGraph.erase(waiterIt);
	}

	class LuaBridgeWaitGraphGuard
	{
		public:
			explicit LuaBridgeWaitGraphGuard(const QThread *waiterThread) : m_waiterThread(waiterThread)
			{
			}
			~LuaBridgeWaitGraphGuard()
			{
				if (m_armed && m_waiterThread && m_targetThread)
					waitGraphLeave(m_waiterThread, m_targetThread);
			}
			[[nodiscard]] bool enter(const QThread *targetThread)
			{
				Q_ASSERT(m_waiterThread);
				Q_ASSERT(targetThread);
				m_armed = waitGraphEnter(m_waiterThread, targetThread);
				if (m_armed)
					m_targetThread = targetThread;
				return m_armed;
			}

		private:
			const QThread *m_waiterThread{nullptr};
			const QThread *m_targetThread{nullptr};
			bool           m_armed{false};
	};

	bool ensureBridgeContextReady(const std::shared_ptr<LuaBridgeThreadContext> &context,
	                              const QString                                 &operationLabel)
	{
		if (!context || context->thread.isNull())
		{
			return setLuaBridgeError(
			    LuaBridgeInvokeStatus::MissingTargetBridgeContext,
			    QStringLiteral("[QMud][LuaBridge] %1 failed: missing target bridge context")
			        .arg(operationLabel));
		}
		if (!QCoreApplication::instance())
		{
			return setLuaBridgeError(
			    LuaBridgeInvokeStatus::NoQCoreApplication,
			    QStringLiteral("[QMud][LuaBridge] %1 failed: no QCoreApplication instance")
			        .arg(operationLabel));
		}
		QThread *const targetThread = context->thread.data();
		if (!targetThread || !targetThread->isRunning())
		{
			return setLuaBridgeError(
			    LuaBridgeInvokeStatus::TargetThreadNotRunning,
			    QStringLiteral("[QMud][LuaBridge] %1 failed for thread %2: target thread is not running")
			        .arg(operationLabel, threadLabel(targetThread)));
		}

		QObject *const invoker = ensureLuaBridgePumpInvoker(context);
		if (!invoker)
		{
			return setLuaBridgeError(
			    LuaBridgeInvokeStatus::BridgeInvokerUnavailable,
			    QStringLiteral("[QMud][LuaBridge] %1 failed for thread %2: unable to create bridge invoker")
			        .arg(operationLabel, threadLabel(targetThread)));
		}
		Q_UNUSED(invoker);

		QDeadlineTimer deadline(kLuaBridgeReadyTimeoutMs);
		for (;;)
		{
			{
				QMutexLocker          locker(&context->mutex);
				QString               runnableReason;
				LuaBridgeInvokeStatus runnableStatus = LuaBridgeInvokeStatus::UnknownFailure;
				if (targetThreadRunnable(targetThread, runnableReason, runnableStatus))
				{
					const bool wasRunning = context->state == LuaBridgeContextState::Running;
					context->state        = LuaBridgeContextState::Running;
					if (!wasRunning)
					{
						++context->wakeSerial;
						context->wake.wakeAll();
					}
					return true;
				}
				if (context->state != LuaBridgeContextState::Stopped)
				{
					context->state      = LuaBridgeContextState::Stopped;
					context->pumpPosted = false;
				}
				if (!targetThread->isRunning())
				{
					return setLuaBridgeError(
					    LuaBridgeInvokeStatus::TargetThreadNotRunning,
					    QStringLiteral(
					        "[QMud][LuaBridge] %1 failed for thread %2: target thread is not running")
					        .arg(operationLabel, threadLabel(targetThread)));
				}
				if (deadline.hasExpired())
				{
					const LuaBridgeInvokeStatus timeoutStatus =
					    runnableStatus == LuaBridgeInvokeStatus::TargetThreadNoEventDispatcher
					        ? LuaBridgeInvokeStatus::TargetThreadNoEventDispatcher
					        : LuaBridgeInvokeStatus::BridgeEndpointNotReadyTimeout;
					return setLuaBridgeError(
					    timeoutStatus,
					    QStringLiteral("[QMud][LuaBridge] %1 timed out waiting for bridge endpoint ready on "
					                   "thread %2 (%3)")
					        .arg(operationLabel, threadLabel(targetThread), runnableReason));
				}
				const qint64  remainingMs = qMax<qint64>(1, qMin<qint64>(deadline.remainingTime(), 100));
				const quint64 wakeSerial  = context->wakeSerial;
				if (remainingMs > 0 && context->state != LuaBridgeContextState::Running &&
				    context->wakeSerial == wakeSerial)
				{
					context->wake.wait(&context->mutex, static_cast<unsigned long>(remainingMs));
				}
			}
		}
	}

	bool enqueueBridgeRequest(const std::shared_ptr<LuaBridgeThreadContext> &context,
	                          const std::shared_ptr<LuaBridgeRequest>       &request)
	{
		if (!context || !request)
			return false;
		{
			QMutexLocker locker(&context->mutex);
			if (context->state != LuaBridgeContextState::Running)
				return false;
			context->queue.append(request);
			++context->wakeSerial;
			context->wake.wakeAll();
		}
		return scheduleLuaBridgePump(context);
	}

	bool pumpLuaBridgeRequestForContext(const std::shared_ptr<LuaBridgeThreadContext> &context)
	{
		if (!context)
			return false;

		std::shared_ptr<LuaBridgeRequest> request;
		{
			QMutexLocker locker(&context->mutex);
			if (context->queue.isEmpty())
				return false;
			request = context->queue.takeFirst();
		}
		if (!request)
			return false;

		if (request->fn)
			request->fn();
		{
			QMutexLocker requestLocker(&request->mutex);
			request->done = true;
			request->wake.wakeAll();
		}
		if (const auto waiterContext = request->waiterContext)
		{
			QMutexLocker waiterLocker(&waiterContext->mutex);
			++waiterContext->wakeSerial;
			waiterContext->wake.wakeAll();
		}
		return true;
	}

	bool scheduleLuaBridgePump(const std::shared_ptr<LuaBridgeThreadContext> &context)
	{
		if (!context || !QCoreApplication::instance())
			return false;
		QObject *const invoker = ensureLuaBridgePumpInvoker(context);
		if (!invoker)
			return false;
		{
			QMutexLocker locker(&context->mutex);
			if (context->state != LuaBridgeContextState::Running)
				return false;
			if (context->pumpPosted)
				return true;
			context->pumpPosted = true;
		}
		QCoreApplication::postEvent(invoker, new LuaBridgePumpEvent());
		return true;
	}

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

bool qmudLuaBridgeEnsureObjectThreadReady(const QObject *target)
{
	clearLuaBridgeError();
	if (!target)
		return setLuaBridgeError(
		    LuaBridgeInvokeStatus::InvalidTargetOrCallback,
		    QStringLiteral("[QMud][LuaBridge] EnsureReady failed: target object is null"));
	QThread *const targetThread = target->thread();
	if (!targetThread)
	{
		return setLuaBridgeError(
		    LuaBridgeInvokeStatus::NoOwningThread,
		    QStringLiteral("[QMud][LuaBridge] EnsureReady failed for target %1: no owning thread")
		        .arg(QString::number(reinterpret_cast<quintptr>(target), 16).toUpper()));
	}
	const std::shared_ptr<LuaBridgeThreadContext> context = contextForThread(targetThread);
	return ensureBridgeContextReady(context, QStringLiteral("EnsureReady"));
}

bool qmudLuaBridgeInvokeOnObjectThread(const QObject *target, const std::function<void()> &fn)
{
	clearLuaBridgeError();
	if (!target || !fn)
		return setLuaBridgeError(
		    LuaBridgeInvokeStatus::InvalidTargetOrCallback,
		    QStringLiteral("[QMud][LuaBridge] Invoke failed: invalid target or callback"));

	QThread *const targetThread = target->thread();
	if (!targetThread)
	{
		return setLuaBridgeError(
		    LuaBridgeInvokeStatus::NoOwningThread,
		    QStringLiteral("[QMud][LuaBridge] Invoke failed for target %1: no owning thread")
		        .arg(QString::number(reinterpret_cast<quintptr>(target), 16).toUpper()));
	}

	QThread *const currentThread = QThread::currentThread();
	if (currentThread == targetThread)
	{
		fn();
		return true;
	}

	const std::shared_ptr<LuaBridgeThreadContext> targetContext = contextForThread(targetThread);
	if (!ensureBridgeContextReady(targetContext, QStringLiteral("Invoke")))
		return false;
	const std::shared_ptr<LuaBridgeThreadContext> currentContext = contextForThread(currentThread);
	if (!currentContext)
	{
		return setLuaBridgeError(
		    LuaBridgeInvokeStatus::MissingCallerBridgeContext,
		    QStringLiteral("[QMud][LuaBridge] Invoke failed for edge %1 -> %2: missing caller bridge context")
		        .arg(threadLabel(currentThread), threadLabel(targetThread)));
	}

	LuaBridgeWaitGraphGuard waitGraphGuard(currentThread);
	if (!waitGraphGuard.enter(targetThread))
		return false;

	const QPointer<const QObject>           targetGuard(target);
	const std::shared_ptr<LuaBridgeRequest> request = std::make_shared<LuaBridgeRequest>();
	request->fn                                     = [targetGuard, fn]
	{
		if (targetGuard)
			fn();
	};
	request->waiterContext = currentContext;

	if (!enqueueBridgeRequest(targetContext, request))
	{
		{
			QMutexLocker locker(&targetContext->mutex);
			targetContext->queue.removeOne(request);
		}
		return setLuaBridgeError(
		    LuaBridgeInvokeStatus::EnqueueFailed,
		    QStringLiteral(
		        "[QMud][LuaBridge] Invoke failed for edge %1 -> %2: unable to enqueue bridge request")
		        .arg(threadLabel(currentThread), threadLabel(targetThread)));
	}

	QDeadlineTimer deadline(kLuaBridgeInvokeTimeoutMs);
	for (;;)
	{
		if (pumpLuaBridgeRequestForContext(currentContext))
			continue;

		{
			QMutexLocker requestLocker(&request->mutex);
			if (request->done)
			{
				if (request->canceled)
				{
					const QString reason =
					    request->cancelReason.isEmpty()
					        ? QStringLiteral(
					              "[QMud][LuaBridge] Invoke failed for edge %1 -> %2: request canceled")
					              .arg(threadLabel(currentThread), threadLabel(targetThread))
					        : request->cancelReason;
					return setLuaBridgeError(request->cancelStatus, reason);
				}
				return true;
			}
		}
		if (deadline.hasExpired())
			break;

		quint64 wakeSerial = 0;
		{
			QMutexLocker waiterLocker(&currentContext->mutex);
			wakeSerial = currentContext->wakeSerial;
		}
		{
			QMutexLocker requestLocker(&request->mutex);
			if (request->done)
			{
				if (request->canceled)
				{
					const QString reason =
					    request->cancelReason.isEmpty()
					        ? QStringLiteral(
					              "[QMud][LuaBridge] Invoke failed for edge %1 -> %2: request canceled")
					              .arg(threadLabel(currentThread), threadLabel(targetThread))
					        : request->cancelReason;
					return setLuaBridgeError(request->cancelStatus, reason);
				}
				return true;
			}
		}
		{
			QMutexLocker waiterLocker(&currentContext->mutex);
			const qint64 waitMs = qMax<qint64>(1, deadline.remainingTime());
			if (currentContext->wakeSerial == wakeSerial)
				currentContext->wake.wait(&currentContext->mutex, static_cast<unsigned long>(waitMs));
		}
	}

	bool canceledBeforeDispatch = false;
	{
		QMutexLocker contextLocker(&targetContext->mutex);
		canceledBeforeDispatch = targetContext->queue.removeOne(request);
	}
	if (canceledBeforeDispatch)
	{
		QMutexLocker requestLocker(&request->mutex);
		if (!request->done)
		{
			request->canceled     = true;
			request->cancelStatus = LuaBridgeInvokeStatus::RequestCanceledBeforeDispatch;
			request->cancelReason =
			    QStringLiteral(
			        "[QMud][LuaBridge] Invoke timed out and canceled queued request for edge %1 -> %2")
			        .arg(threadLabel(currentThread), threadLabel(targetThread));
			request->done = true;
			request->wake.wakeAll();
		}
		const QString reason =
		    request->cancelReason.isEmpty()
		        ? QStringLiteral(
		              "[QMud][LuaBridge] Invoke timed out and canceled queued request for edge %1 -> %2")
		              .arg(threadLabel(currentThread), threadLabel(targetThread))
		        : request->cancelReason;
		return setLuaBridgeError(request->cancelStatus, reason);
	}

	{
		QMutexLocker requestLocker(&request->mutex);
		if (request->done)
		{
			if (request->canceled)
			{
				const QString reason =
				    request->cancelReason.isEmpty()
				        ? QStringLiteral(
				              "[QMud][LuaBridge] Invoke failed for edge %1 -> %2: request canceled")
				              .arg(threadLabel(currentThread), threadLabel(targetThread))
				        : request->cancelReason;
				return setLuaBridgeError(request->cancelStatus, reason);
			}
			return true;
		}
	}

	const QString timeoutMessage = QStringLiteral("[QMud][LuaBridge] Invoke timed out after %1 ms while "
	                                              "request was already executing for edge %2 -> %3")
	                                   .arg(kLuaBridgeInvokeTimeoutMs)
	                                   .arg(threadLabel(currentThread), threadLabel(targetThread));
	return setLuaBridgeError(LuaBridgeInvokeStatus::TimedOutInFlight, timeoutMessage);
}

QString qmudLuaBridgeLastError()
{
	return t_luaBridgeLastError;
}

LuaBridgeInvokeStatus qmudLuaBridgeLastStatus()
{
	return t_luaBridgeLastStatus;
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
	Q_UNREACHABLE_RETURN(LuaExecutorBridgePolicy::UiSync);
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
