/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: tst_MainFrameMdiUtils.cpp
 * Role: QTest coverage for MainFrame MDI fallback/restore helper behavior.
 */

#include "MainFrameMdiUtils.h"

#include <QMdiSubWindow>
#include <QVariant>
#include <QtTest/QTest>

/**
 * @brief QTest fixture covering MainFrame MDI helper scenarios.
 */
class tst_MainFrameMdiUtils : public QObject
{
		Q_OBJECT

		// NOLINTBEGIN(readability-convert-member-functions-to-static)
	private slots:
		void resolveCurrentOrLastPrefersCurrentActive()
		{
			QMdiSubWindow                current;
			QMdiSubWindow                last;
			const QList<QMdiSubWindow *> windows{&current, &last};
			QCOMPARE(QMudMainFrameMdiUtils::resolveCurrentOrLastActiveSubWindow(&current, &last, windows),
			         &current);
		}

		void resolveCurrentOrLastFallsBackToLastActive()
		{
			QMdiSubWindow                last;
			const QList<QMdiSubWindow *> windows{&last};
			QCOMPARE(QMudMainFrameMdiUtils::resolveCurrentOrLastActiveSubWindow(nullptr, &last, windows),
			         &last);
		}

		void resolveCurrentOrLastReturnsNullWhenLastMissing()
		{
			QMdiSubWindow                last;
			const QList<QMdiSubWindow *> windows;
			QCOMPARE(QMudMainFrameMdiUtils::resolveCurrentOrLastActiveSubWindow(nullptr, &last, windows),
			         nullptr);
		}

		void resolveBackgroundAddRestoreTargetUsesLastWhenCurrentIsNull()
		{
			QMdiSubWindow                last;
			QMdiSubWindow                added;
			const QList<QMdiSubWindow *> windows{&last};
			QCOMPARE(
			    QMudMainFrameMdiUtils::resolveBackgroundAddRestoreTarget(nullptr, &last, windows, &added),
			    &last);
		}

		void resolveBackgroundAddRestoreTargetIgnoresAddedWindow()
		{
			QMdiSubWindow                added;
			const QList<QMdiSubWindow *> windows{&added};
			QCOMPARE(
			    QMudMainFrameMdiUtils::resolveBackgroundAddRestoreTarget(&added, &added, windows, &added),
			    nullptr);
		}

		void windowMatchesRuntimeIdentityPrefersRuntimeToken()
		{
			QMdiSubWindow notepad;
			notepad.setProperty("worldRuntimeToken", QVariant::fromValue<qulonglong>(42));
			notepad.setProperty("worldId", QStringLiteral("other-world"));

			QVERIFY(QMudMainFrameMdiUtils::windowMatchesRuntimeIdentity(&notepad, 42,
			                                                            QStringLiteral("world-a"), false));
		}

		void windowMatchesRuntimeIdentityRejectsMismatchedRuntimeToken()
		{
			QMdiSubWindow notepad;
			notepad.setProperty("worldRuntimeToken", QVariant::fromValue<qulonglong>(7));

			QVERIFY(!QMudMainFrameMdiUtils::windowMatchesRuntimeIdentity(&notepad, 42,
			                                                             QStringLiteral("world-a"), true));
		}

		void windowMatchesRuntimeIdentityUsesWorldIdWhenTokenIsAbsent()
		{
			QMdiSubWindow notepad;
			notepad.setProperty("worldId", QStringLiteral("WORLD-A"));

			QVERIFY(QMudMainFrameMdiUtils::windowMatchesRuntimeIdentity(&notepad, 42,
			                                                            QStringLiteral("world-a"), false));
		}

		void windowMatchesRuntimeIdentityKeepsUnownedOutOfStrictMatches()
		{
			QMdiSubWindow notepad;

			QVERIFY(!QMudMainFrameMdiUtils::windowMatchesRuntimeIdentity(&notepad, 42,
			                                                             QStringLiteral("world-a"), false));
			QVERIFY(QMudMainFrameMdiUtils::windowMatchesRuntimeIdentity(&notepad, 42,
			                                                            QStringLiteral("world-a"), true));
		}

		void firstWindowMatchingRuntimeIdentityUsesCreationOrder()
		{
			QMdiSubWindow unrelated;
			unrelated.setProperty("worldRuntimeToken", QVariant::fromValue<qulonglong>(7));
			QMdiSubWindow first;
			first.setProperty("worldRuntimeToken", QVariant::fromValue<qulonglong>(42));
			QMdiSubWindow second;
			second.setProperty("worldRuntimeToken", QVariant::fromValue<qulonglong>(42));

			const QList<QMdiSubWindow *> windows{&unrelated, &first, &second};
			QCOMPARE(QMudMainFrameMdiUtils::firstWindowMatchingRuntimeIdentity(
			             windows, 42, QStringLiteral("world-a"), false),
			         &first);
		}

		void prepareOpenWorldStateBeforeChildCloseAllowsCloseWithoutController()
		{
			QString error = QStringLiteral("stale");
			QVERIFY(QMudMainFrameMdiUtils::prepareOpenWorldStateBeforeChildClose({}, &error));
			QVERIFY(error.isEmpty());
		}

		void prepareOpenWorldStateBeforeChildCloseRunsSaveBeforeAllowingClose()
		{
			QString    error   = QStringLiteral("stale");
			bool       saved   = false;
			const bool proceed = QMudMainFrameMdiUtils::prepareOpenWorldStateBeforeChildClose(
			    [&saved](QString *errorMessage)
			    {
				    saved = true;
				    if (errorMessage)
					    errorMessage->clear();
				    return true;
			    },
			    &error);

			QVERIFY(proceed);
			QVERIFY(saved);
			QVERIFY(error.isEmpty());
		}

		void prepareOpenWorldStateBeforeChildCloseStopsCloseOnSaveFailure()
		{
			QString    error;
			bool       saved   = false;
			const bool proceed = QMudMainFrameMdiUtils::prepareOpenWorldStateBeforeChildClose(
			    [&saved](QString *errorMessage)
			    {
				    saved = true;
				    if (errorMessage)
					    *errorMessage = QStringLiteral("session write failed");
				    return false;
			    },
			    &error);

			QVERIFY(!proceed);
			QVERIFY(saved);
			QCOMPARE(error, QStringLiteral("session write failed"));
		}
		// NOLINTEND(readability-convert-member-functions-to-static)
};

QTEST_MAIN(tst_MainFrameMdiUtils)

#if __has_include("tst_MainFrameMdiUtils.moc")
#include "tst_MainFrameMdiUtils.moc"
#endif
