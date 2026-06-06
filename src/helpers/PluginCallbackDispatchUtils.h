/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: PluginCallbackDispatchUtils.h
 * Role: Shared helpers for Lua plugin-callback dispatch policy decisions.
 */

#ifndef QMUD_PLUGINCALLBACKDISPATCHUTILS_H
#define QMUD_PLUGINCALLBACKDISPATCHUTILS_H

/**
 * @brief Returns whether a contended hotspot callback should be queued instead of synchronously waited on.
 * @param queueWhenCallbackLaneBusy Caller opt-in for the narrow non-blocking hotspot path.
 * @param dispatchActive `true` when a callback dispatch is currently active on the runtime thread.
 * @param workerInFlight `true` when callback-lane worker execution is in progress.
 * @param hasQueuedDispatches `true` when the callback dispatch queue already contains work.
 * @param drainQueued `true` when a callback dispatch drain has been posted but not run yet.
 * @return `true` when the callback should be queued asynchronously to avoid blocking the caller.
 */
[[nodiscard]] constexpr bool qmudShouldQueueContendedHotspotCallback(const bool queueWhenCallbackLaneBusy,
                                                                     const bool dispatchActive,
                                                                     const bool workerInFlight,
                                                                     const bool hasQueuedDispatches,
                                                                     const bool drainQueued) noexcept
{
	return queueWhenCallbackLaneBusy &&
	       (dispatchActive || workerInFlight || hasQueuedDispatches || drainQueued);
}

#endif // QMUD_PLUGINCALLBACKDISPATCHUTILS_H
