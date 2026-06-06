/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: LuaModalDialogUtils.h
 * Role: Lua dialog dispatch and menu UI helpers.
 */

#ifndef QMUD_HELPERS_LUAMODALDIALOGUTILS_H
#define QMUD_HELPERS_LUAMODALDIALOGUTILS_H

#include "helpers/LuaExecutionUtils.h"

#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QString>
// ReSharper disable once CppUnusedIncludeDirective
#include <QStringList>
#include <QThread>

#include <memory>
#include <type_traits>
#include <utility>

struct MiniWindow;
class QWidget;
class WorldView;

/**
 * @brief Runs a GUI-thread modal dialog and returns its result to the Lua worker synchronously.
 *
 * This helper is intentionally separate from the runtime/callback-lane bridge. It dispatches the supplied GUI
 * callable through the modal bridge, which lets GUI-side synchronous callback waits execute the dialog while the
 * Lua worker waits only for this dialog result and does not pump deferred Lua/runtime work.
 *
 * @tparam Func Callable type with no arguments.
 * @param func GUI-thread callable that opens the modal dialog and returns the Lua-visible result value.
 * @param fallbackValue Value returned if the application object is unavailable, dispatch fails, or shutdown wakes
 * the wait before the dialog callable completes.
 * @return Dialog result, or @p fallbackValue on failure/shutdown.
 */
template <typename Func>
auto qmudRunGuiModalDialogSync(Func &&func, std::invoke_result_t<std::decay_t<Func>> fallbackValue)
    -> std::invoke_result_t<std::decay_t<Func>>
{
	using DecayedFunc = std::decay_t<Func>;
	using ReturnType  = std::invoke_result_t<DecayedFunc>;
	static_assert(!std::is_void_v<ReturnType>, "Lua modal dialog callables must return a value");

	QPointer<QCoreApplication> app = QCoreApplication::instance();
	if (!app)
		return fallbackValue;

	if (QThread::currentThread() == app->thread())
		return std::forward<Func>(func)();

	auto       funcCopy = std::make_shared<DecayedFunc>(std::forward<Func>(func));
	ReturnType result   = std::move(fallbackValue);
	static_cast<void>(qmudLuaBridgeInvokeModalOnObjectThread(app.data(), [&] { result = (*funcCopy)(); }));
	return result;
}

/**
 * @brief Shows the Lua `Menu` selection dialog.
 * @param view World view used to position the dialog near the input cursor.
 * @param items Sorted and de-duplicated menu items.
 * @param def Default selected item text.
 * @param parent Optional parent widget for modal ownership.
 * @return Selected item text, or an empty string when canceled/unavailable.
 */
[[nodiscard]] QString qmudShowLuaMenuDialog(const WorldView *view, const QStringList &items,
                                            const QString &def, QWidget *parent = nullptr);

/**
 * @brief Shows the Lua `WindowMenu` popup for a miniwindow snapshot.
 * @param view World view used to translate miniwindow-local coordinates to global coordinates.
 * @param window Miniwindow state or callback snapshot used for bounds and placement.
 * @param left Miniwindow-local x coordinate.
 * @param top Miniwindow-local y coordinate.
 * @param items Lua menu grammar string.
 * @return Selected item text/number according to the menu grammar, or an empty string when canceled/unavailable.
 */
[[nodiscard]] QString qmudShowLuaMiniWindowMenuDialog(WorldView *view, const MiniWindow *window, int left,
                                                      int top, const QString &items);

#endif // QMUD_HELPERS_LUAMODALDIALOGUTILS_H
