/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: LuaExecutorWorker.h
 * Role: Worker-thread Lua executor backend.
 */

#ifndef QMUD_LUAEXECUTORWORKER_H
#define QMUD_LUAEXECUTORWORKER_H

#include "LuaExecutor.h"

#include <QMutex>
// ReSharper disable once CppUnusedIncludeDirective
#include <QObject>
#include <QQueue>
#include <QSharedPointer>
#include <QThread>
#include <atomic>

/**
 * @brief Worker-thread executor backend for Lua callback dispatch.
 *
 * All callback commands are enqueued to the worker. Barrier commands wait for queued
 * completion results, while non-barrier commands return immediately after enqueue.
 */
class LuaExecutorWorker final : public ILuaExecutor
{
	public:
		LuaExecutorWorker();
		~LuaExecutorWorker() override;

		LuaBatchDispatchResult dispatchBatch(const LuaBatchDispatchRequest &request) const override;
		void                   dispatchBatchAsync(const LuaBatchDispatchRequest &request) const override;
		void                   dispatchBatchAsync(
		    const LuaBatchDispatchRequest &request, QObject *completionTarget,
		    const std::function<void(const LuaBatchDispatchResult &)> &completion) const override;

	private:
		struct QueuedDispatchRequest;
		struct WorkerLaneState;
		[[nodiscard]] WorkerLaneState   &laneStateForRequest(const LuaBatchDispatchRequest &request) const;
		[[nodiscard]] static const char *laneName(const WorkerLaneState &lane);
		bool        enqueueQueuedDispatch(WorkerLaneState                             &lane,
		                                  const QSharedPointer<QueuedDispatchRequest> &request) const;
		bool        processOneQueuedDispatch(WorkerLaneState &lane) const;
		void        processQueuedDispatches(WorkerLaneState &lane) const;
		static void initializeWorkerLane(WorkerLaneState &lane, const QString &threadName);
		static void shutdownWorkerLane(WorkerLaneState &lane);
		void        shutdownWorker() const;

		struct WorkerLaneState
		{
				const char                                   *name{nullptr};
				std::unique_ptr<QThread>                      thread;
				std::unique_ptr<QObject>                      invoker;
				std::atomic_bool                              bridgeReady{false};
				QMutex                                        dispatchQueueMutex;
				QQueue<QSharedPointer<QueuedDispatchRequest>> dispatchQueue;
				bool                                          dispatchQueuePumpQueued{false};
		};

		mutable WorkerLaneState  m_controlLane;
		mutable WorkerLaneState  m_callbackLane;
		mutable std::atomic_bool m_workerShuttingDown{false};
		LuaExecutorDirect        m_direct;
};

#endif // QMUD_LUAEXECUTORWORKER_H
