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
#include <QVariant>

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

	bool windowMatchesRuntimeIdentity(const QMdiSubWindow *window, const qulonglong ownerToken,
	                                  const QString &ownerWorldId, const bool acceptUnowned)
	{
		if (!window)
			return false;

		const qulonglong relatedToken = window->property("worldRuntimeToken").toULongLong();
		if (ownerToken != 0)
		{
			if (relatedToken == ownerToken)
				return true;
			if (relatedToken != 0)
				return false;
		}

		const QString relatedWorldId = window->property("worldId").toString().trimmed();
		if (!ownerWorldId.isEmpty())
		{
			if (!relatedWorldId.isEmpty())
				return relatedWorldId.compare(ownerWorldId, Qt::CaseInsensitive) == 0;
			return acceptUnowned;
		}

		return acceptUnowned || (ownerToken == 0 && relatedToken == 0 && relatedWorldId.isEmpty());
	}

	QMdiSubWindow *firstWindowMatchingRuntimeIdentity(const QList<QMdiSubWindow *> &windows,
	                                                  const qulonglong              ownerToken,
	                                                  const QString &ownerWorldId, const bool acceptUnowned)
	{
		for (QMdiSubWindow *window : windows)
		{
			if (windowMatchesRuntimeIdentity(window, ownerToken, ownerWorldId, acceptUnowned))
				return window;
		}

		return nullptr;
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
