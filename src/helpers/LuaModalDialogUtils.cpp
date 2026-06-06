/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: LuaModalDialogUtils.cpp
 * Role: Lua dialog helper implementation.
 */

#include "helpers/LuaModalDialogUtils.h"

#include "MiniWindow.h"
#include "StringUtils.h"
#include "WorldView.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QListWidget>
#include <QMenu>
// ReSharper disable once CppUnusedIncludeDirective
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QStack>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
	class ContextMenuDismissReplayFilter final : public QObject
	{
		public:
			explicit ContextMenuDismissReplayFilter(const QMenu *menu) : m_menu(menu)
			{
			}

			[[nodiscard]] bool hasReplayPoint() const
			{
				return m_hasReplayPoint;
			}

			[[nodiscard]] QPoint replayPoint() const
			{
				return m_replayPoint;
			}

			bool eventFilter(QObject *watched, QEvent *event) override
			{
				if (const auto *watchedWidget = qobject_cast<QWidget *>(watched);
				    m_menu && watchedWidget &&
				    (watchedWidget == m_menu || m_menu->isAncestorOf(watchedWidget)))
					return false;

				if (event->type() == QEvent::MouseButtonPress)
				{
					if (const auto *mouse = dynamic_cast<QMouseEvent *>(event);
					    mouse && mouse->button() == Qt::RightButton)
					{
						m_hasReplayPoint = true;
						m_replayPoint    = mouse->globalPosition().toPoint();
					}
				}
				else if (event->type() == QEvent::ContextMenu)
				{
					if (const auto *contextEvent = dynamic_cast<QContextMenuEvent *>(event);
					    contextEvent && contextEvent->reason() == QContextMenuEvent::Mouse)
					{
						m_hasReplayPoint = true;
						m_replayPoint    = contextEvent->globalPos();
					}
				}
				return false;
			}

		private:
			const QMenu *m_menu{nullptr};
			bool         m_hasReplayPoint{false};
			QPoint       m_replayPoint;
	};

	enum class WindowMenuAlignH
	{
		Left,
		Center,
		Right
	};

	enum class WindowMenuAlignV
	{
		Top,
		Center,
		Bottom
	};
} // namespace

QString qmudShowLuaMenuDialog(const WorldView *view, const QStringList &items, const QString &def,
                              QWidget *parent)
{
	if (items.isEmpty())
		return {};

	QDialog dialog(parent);
	dialog.setWindowTitle(QStringLiteral("Menu"));
	dialog.setWindowFlags(dialog.windowFlags() | Qt::Tool);
	QVBoxLayout layout(&dialog);
	QListWidget list;
	list.addItems(items);
	layout.addWidget(&list);

	QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout.addWidget(&buttons);

	QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(&list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

	if (!def.isEmpty())
	{
		if (const QList<QListWidgetItem *> matches = list.findItems(def, Qt::MatchExactly);
		    !matches.isEmpty())
			list.setCurrentItem(matches.first());
	}
	if (view && view->inputEditor())
	{
		QPlainTextEdit *input  = view->inputEditor();
		QTextCursor     cursor = input->textCursor();
		cursor.setPosition(cursor.selectionEnd());
		const QRect  rect   = input->cursorRect(cursor);
		const QPoint global = input->mapToGlobal(rect.bottomLeft());
		dialog.move(global);
	}

	if (dialog.exec() != QDialog::Accepted)
		return {};
	QListWidgetItem *selected = list.currentItem();
	return selected ? selected->text() : QString();
}

QString qmudShowLuaMiniWindowMenuDialog(WorldView *view, const MiniWindow *window, const int left,
                                        const int top, const QString &items)
{
	if (!view || !window)
		return {};

	if (!window->show || window->temporarilyHide)
		return {};

	if (left < 0 || left > window->width || top < 0 || top > window->height)
		return {};

	QString menuText = items;
	if (menuText.isEmpty())
		return {};

	bool returnNumber = false;
	if (!menuText.isEmpty() && menuText.at(0) == QLatin1Char('!'))
	{
		returnNumber = true;
		menuText.remove(0, 1);
	}

	auto alignH = WindowMenuAlignH::Left;
	auto alignV = WindowMenuAlignV::Top;
	if (!menuText.isEmpty() && menuText.at(0) == QLatin1Char('~') && menuText.size() > 3)
	{
		const QChar h = menuText.at(1);
		const QChar v = menuText.at(2);
		menuText.remove(0, 3);
		switch (h.toLower().unicode())
		{
		case 'c':
			alignH = WindowMenuAlignH::Center;
			break;
		case 'r':
			alignH = WindowMenuAlignH::Right;
			break;
		default:
			break;
		}
		switch (v.toLower().unicode())
		{
		case 'c':
			alignV = WindowMenuAlignV::Center;
			break;
		case 'b':
			alignV = WindowMenuAlignV::Bottom;
			break;
		default:
			break;
		}
	}

	const QStringList parts = qmudSplitLegacyMenuItems(menuText);
	QMenu             menu;
	menu.setAttribute(Qt::WA_NoMouseReplay, false);
	QStack<QMenu *> stack;
	stack.push(&menu);
	int           itemIndex        = 1;
	constexpr int kMxpMenuCount    = 100;
	bool          reachedMenuLimit = false;

	auto createAction = [&](QMenu *parent, const QString &text, const bool checked, const bool disabled)
	{
		QAction *action = parent->addAction(text);
		action->setEnabled(!disabled);
		if (checked)
		{
			action->setCheckable(true);
			action->setChecked(true);
		}
		if (!disabled)
		{
			if (itemIndex > kMxpMenuCount)
			{
				reachedMenuLimit = true;
				return static_cast<QAction *>(nullptr);
			}
			action->setData(itemIndex++);
			if (itemIndex > kMxpMenuCount)
				reachedMenuLimit = true;
		}
		else
		{
			action->setData(0);
		}
		return action;
	};

	for (QString part : parts)
	{
		if (part.isEmpty() || part == QLatin1String("-"))
		{
			stack.top()->addSeparator();
			continue;
		}
		if (part.startsWith(QLatin1Char('>')))
		{
			stack.push(stack.top()->addMenu(part.mid(1)));
			continue;
		}
		if (part.startsWith(QLatin1Char('<')))
		{
			if (stack.size() > 1)
				stack.pop();
			continue;
		}
		bool checked  = false;
		bool disabled = false;
		while (!part.isEmpty() && (part.at(0) == QLatin1Char('+') || part.at(0) == QLatin1Char('^')))
		{
			if (part.at(0) == QLatin1Char('+'))
				checked = true;
			else if (part.at(0) == QLatin1Char('^'))
				disabled = true;
			part.remove(0, 1);
		}
		if (!createAction(stack.top(), part, checked, disabled) || reachedMenuLimit)
			break;
	}

	QPoint      globalPos = view->miniWindowGlobalPosition(window, left, top);
	const QSize hint      = menu.sizeHint();
	if (alignH == WindowMenuAlignH::Center)
		globalPos.setX(globalPos.x() - hint.width() / 2);
	else if (alignH == WindowMenuAlignH::Right)
		globalPos.setX(globalPos.x() - hint.width());
	if (alignV == WindowMenuAlignV::Center)
		globalPos.setY(globalPos.y() - hint.height() / 2);
	else if (alignV == WindowMenuAlignV::Bottom)
		globalPos.setY(globalPos.y() - hint.height());

	ContextMenuDismissReplayFilter replayFilter(&menu);
	qApp->installEventFilter(&replayFilter);
	QAction const *selected = menu.exec(globalPos);
	qApp->removeEventFilter(&replayFilter);

	if (!selected && replayFilter.hasReplayPoint())
	{
		const QPoint        replayPos = replayFilter.replayPoint();
		QPointer<WorldView> viewGuard = view;
		QTimer::singleShot(0, qApp,
		                   [viewGuard, replayPos]
		                   {
			                   if (viewGuard)
				                   viewGuard->showContextMenuAtGlobalPos(replayPos);
		                   });
	}

	if (!selected)
		return {};

	if (returnNumber)
		return QString::number(selected->data().toInt());
	return selected->text();
}
