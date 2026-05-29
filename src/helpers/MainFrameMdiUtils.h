/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: MainFrameMdiUtils.h
 * Role: Pure helpers for main-frame MDI state and shutdown preparation behavior.
 */

#ifndef QMUD_MAINFRAMEMDIUTILS_H
#define QMUD_MAINFRAMEMDIUTILS_H

class QMdiSubWindow;
class QString;
template <typename T> class QList;

#include <functional>

namespace QMudMainFrameMdiUtils
{
	/**
	 * @brief Resolves the effective active subwindow using current active first, then last active fallback.
	 * @param active Current active subwindow from QMdiArea.
	 * @param lastActive Last known active subwindow tracked by MainWindow.
	 * @param creationOrder Current QMdiArea creation-order window list.
	 * @return Active/fallback subwindow when valid, otherwise `nullptr`.
	 */
	QMdiSubWindow *resolveCurrentOrLastActiveSubWindow(QMdiSubWindow *active, QMdiSubWindow *lastActive,
	                                                   const QList<QMdiSubWindow *> &creationOrder);

	/**
	 * @brief Resolves which subwindow should be restored after adding a window with activation disabled.
	 * @param active Current active subwindow from QMdiArea.
	 * @param lastActive Last known active subwindow tracked by MainWindow.
	 * @param creationOrder Current QMdiArea creation-order window list.
	 * @param addedSubWindow Newly added subwindow.
	 * @return Restore target, or `nullptr` when no restore is needed.
	 */
	QMdiSubWindow *resolveBackgroundAddRestoreTarget(QMdiSubWindow *active, QMdiSubWindow *lastActive,
	                                                 const QList<QMdiSubWindow *> &creationOrder,
	                                                 const QMdiSubWindow          *addedSubWindow);

	/**
	 * @brief Runs open-world persistence before child windows are closed.
	 * @param saveOpenWorldState Persistence callback, or empty when no controller is available.
	 * @param errorMessage Optional output error text when persistence fails.
	 * @return `true` when shutdown can continue to child-window close.
	 */
	[[nodiscard]] bool
	prepareOpenWorldStateBeforeChildClose(const std::function<bool(QString *)> &saveOpenWorldState,
	                                      QString                              *errorMessage = nullptr);
} // namespace QMudMainFrameMdiUtils

#endif // QMUD_MAINFRAMEMDIUTILS_H
