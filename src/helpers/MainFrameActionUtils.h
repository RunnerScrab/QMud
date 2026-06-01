/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: MainFrameActionUtils.h
 * Role: Pure helpers for main-frame action ids, tooltips, and platform menu policy.
 */

#ifndef QMUD_MAINFRAMEACTIONUTILS_H
#define QMUD_MAINFRAMEACTIONUTILS_H

#include <QAction>
#include <QKeySequence>

class QString;

namespace QMudMainFrameActionUtils
{
	/**
	 * @brief Builds command id string for one world-toolbar slot.
	 * @param slot 1-based slot index.
	 * @return Command name used by action dispatch.
	 */
	QString                         worldCommandNameForSlot(int slot);

	/**
	 * @brief Builds user-facing toolbar tooltip text for one world slot.
	 * @param slot 1-based slot index.
	 * @return Tooltip string including shortcut hint when available.
	 */
	QString                         worldButtonTooltipForSlot(int slot);
	/**
	 * @brief Resolves native menu role for a main-frame command action.
	 * @param commandName Command identifier.
	 * @return Qt menu role used for platform menu integration.
	 */
	[[nodiscard]] QAction::MenuRole menuRoleForCommand(const QString &commandName);
	/**
	 * @brief Resolves keyboard shortcut for a main-frame command action.
	 * @param commandName Command identifier.
	 * @param configuredShortcut Shortcut explicitly assigned by caller.
	 * @return Shortcut that should be installed on the action.
	 */
	[[nodiscard]] QKeySequence shortcutForCommand(const QString      &commandName,
	                                              const QKeySequence &configuredShortcut = QKeySequence());
	/**
	 * @brief Builds toolbar tooltip text with a platform-native shortcut suffix.
	 * @param label User-facing tooltip label.
	 * @param portableShortcut Shortcut in Qt portable text form.
	 * @return Tooltip label with native shortcut hint appended when available.
	 */
	[[nodiscard]] QString toolbarTooltipWithShortcut(const QString &label, const QString &portableShortcut);
	/**
	 * @brief Returns whether incoming server line should attempt taskbar flash.
	 * @param worldFlashEnabled `true` when world flash option is enabled.
	 * @param appFocused `true` when QMud currently has application focus.
	 * @return `true` when runtime should attempt flash request.
	 */
	[[nodiscard]] bool    shouldAttemptIncomingLineTaskbarFlash(bool worldFlashEnabled, bool appFocused);
	/**
	 * @brief Resolves app-focused state for taskbar flash gating from Qt/main-window focus signals.
	 * @param qtAppFocused `true` when Qt reports ApplicationActive.
	 * @param windowFocused `true` when main-window focus tracker reports focused.
	 * @return `true` when flash logic should treat app as focused.
	 */
	[[nodiscard]] bool    resolveIncomingLineFocusForFlash(bool qtAppFocused, bool windowFocused);
	/**
	 * @brief Resolves app-focused state for activity-sound gating from Qt/main-window focus signals.
	 * @param qtAppFocused `true` when Qt reports ApplicationActive.
	 * @param windowFocused `true` when main-window focus tracker reports focused.
	 * @return `true` when sound logic should treat app as focused.
	 */
	[[nodiscard]] bool    resolveIncomingLineFocusForActivitySound(bool qtAppFocused, bool windowFocused);
	/**
	 * @brief Returns whether main window should issue a background flash request now.
	 * @param appFocused `true` when QMud currently has application focus.
	 * @param flashAlreadyRequested `true` when background session has already requested flash.
	 * @return `true` when a new flash request should be issued.
	 */
	[[nodiscard]] bool    shouldRequestBackgroundTaskbarFlash(bool appFocused, bool flashAlreadyRequested);
	/**
	 * @brief Returns whether focus transition should reset background flash latch.
	 * @param previousFocused Previous application focus state.
	 * @param currentFocused Current application focus state.
	 * @return `true` when focus state changed and latch should reset.
	 */
	[[nodiscard]] bool    shouldResetBackgroundFlashLatch(bool previousFocused, bool currentFocused);
} // namespace QMudMainFrameActionUtils

#endif // QMUD_MAINFRAMEACTIONUTILS_H
