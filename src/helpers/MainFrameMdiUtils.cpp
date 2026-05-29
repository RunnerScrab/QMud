/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: MainFrameMdiUtils.cpp
 * Role: Pure helpers for main-frame MDI state and shutdown preparation behavior.
 */

#include "MainFrameMdiUtils.h"

// ReSharper disable once CppUnusedIncludeDirective
#include <QList>
#include <QMdiSubWindow>
#include <QString>

namespace QMudMainFrameMdiUtils
{
	QMdiSubWindow *resolveCurrentOrLastActiveSubWindow(QMdiSubWindow *active, QMdiSubWindow *lastActive,
	                                                   const QList<QMdiSubWindow *> &creationOrder)
	{
		if (active)
			return active;
		if (lastActive && creationOrder.contains(lastActive))
			return lastActive;
		return nullptr;
	}

	QMdiSubWindow *resolveBackgroundAddRestoreTarget(QMdiSubWindow *active, QMdiSubWindow *lastActive,
	                                                 const QList<QMdiSubWindow *> &creationOrder,
	                                                 const QMdiSubWindow          *addedSubWindow)
	{
		QMdiSubWindow *const target = resolveCurrentOrLastActiveSubWindow(active, lastActive, creationOrder);
		if (!target || target == addedSubWindow)
			return nullptr;
		return target;
	}

	bool prepareOpenWorldStateBeforeChildClose(const std::function<bool(QString *)> &saveOpenWorldState,
	                                           QString                              *errorMessage)
	{
		if (errorMessage)
			errorMessage->clear();
		if (!saveOpenWorldState)
			return true;

		QString saveError;
		if (saveOpenWorldState(&saveError))
			return true;

		if (errorMessage)
			*errorMessage = saveError;
		return false;
	}
} // namespace QMudMainFrameMdiUtils
