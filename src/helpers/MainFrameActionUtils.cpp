/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: MainFrameActionUtils.cpp
 * Role: Pure helpers for main-frame action ids, tooltips, and platform menu policy.
 */

#include "MainFrameActionUtils.h"

#include <QString>

namespace QMudMainFrameActionUtils
{
	QString worldCommandNameForSlot(const int slot)
	{
		return QStringLiteral("World%1").arg(slot);
	}

	QString worldButtonTooltipForSlot(const int slot)
	{
		if (slot >= 1 && slot <= 9)
			return toolbarTooltipWithShortcut(QStringLiteral("Activates world #%1").arg(slot),
			                                  QStringLiteral("Ctrl+%1").arg(slot));
		if (slot == 10)
			return toolbarTooltipWithShortcut(QStringLiteral("Activates world #10"),
			                                  QStringLiteral("Ctrl+0"));
		return QStringLiteral("Activates world #%1").arg(slot);
	}

	QAction::MenuRole menuRoleForCommand(const QString &commandName)
	{
		if (commandName == QStringLiteral("ExitClient"))
			return QAction::QuitRole;
#ifdef Q_OS_MACOS
		Q_UNUSED(commandName);
		return QAction::NoRole;
#else
		if (commandName == QStringLiteral("QuitFromWorld"))
			return QAction::NoRole;
		return QAction::TextHeuristicRole;
#endif
	}

	QKeySequence shortcutForCommand(const QString &commandName, const QKeySequence &configuredShortcut)
	{
		if (commandName == QStringLiteral("ExitClient") && configuredShortcut.isEmpty())
			return {QKeySequence::Quit};
		return configuredShortcut;
	}

	QString toolbarTooltipWithShortcut(const QString &label, const QString &portableShortcut)
	{
		const QKeySequence shortcut = QKeySequence::fromString(portableShortcut, QKeySequence::PortableText);
		const QString      nativeShortcut = shortcut.toString(QKeySequence::NativeText);
		if (nativeShortcut.isEmpty())
			return label;
		return QStringLiteral("%1 (%2)").arg(label, nativeShortcut);
	}

	int adjacentTabShortcutStep(const int key, const Qt::KeyboardModifiers modifiers)
	{
		const bool hasOnlyCtrlShift =
		    modifiers.testFlag(Qt::ControlModifier) && modifiers.testFlag(Qt::ShiftModifier) &&
		    !modifiers.testFlag(Qt::AltModifier) && !modifiers.testFlag(Qt::MetaModifier);
		if (!hasOnlyCtrlShift)
			return 0;
		if (key == Qt::Key_Left)
			return -1;
		if (key == Qt::Key_Right)
			return 1;
		return 0;
	}

	bool shouldAttemptIncomingLineTaskbarFlash(const bool worldFlashEnabled, const bool appFocused)
	{
		return worldFlashEnabled && !appFocused;
	}

	bool resolveIncomingLineFocusForFlash(const bool qtAppFocused, const bool windowFocused)
	{
		return qtAppFocused || windowFocused;
	}

	bool resolveIncomingLineFocusForActivitySound(const bool qtAppFocused, const bool windowFocused)
	{
		return qtAppFocused && windowFocused;
	}

	bool shouldRequestBackgroundTaskbarFlash(const bool appFocused, const bool flashAlreadyRequested)
	{
		return !appFocused && !flashAlreadyRequested;
	}

	bool shouldResetBackgroundFlashLatch(const bool previousFocused, const bool currentFocused)
	{
		return previousFocused != currentFocused;
	}
} // namespace QMudMainFrameActionUtils
