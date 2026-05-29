/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: tst_MiniWindowUtils.cpp
 * Role: QTest coverage for MiniWindowUtils behavior.
 */

#include "MiniWindowUtils.h"

#include "MiniWindow.h"
#include "scripting/ScriptingErrors.h"

#include <QtTest/QTest>

/**
 * @brief QTest fixture covering MiniWindowUtils scenarios.
 */
class tst_MiniWindowUtils : public QObject
{
		Q_OBJECT

		// NOLINTBEGIN(readability-convert-member-functions-to-static)
	private slots:
		void lineFitsVerticallyAllowsExactBottomFit()
		{
			QVERIFY(MiniWindowUtils::lineFitsVertically(10, 11, 20));
		}

		void lineFitsVerticallyRejectsOverflowPastBottom()
		{
			QVERIFY(!MiniWindowUtils::lineFitsVertically(10, 12, 20));
		}

		void runNeedsWrapDoesNotWrapOnExactRightFit()
		{
			QVERIFY(!MiniWindowUtils::runNeedsWrap(5, 15, 10, 5, 20));
		}

		void runNeedsWrapWrapsWhenCandidateExceedsRightBound()
		{
			QVERIFY(MiniWindowUtils::runNeedsWrap(5, 16, 10, 5, 20));
		}

		void runNeedsWrapSkipsWrapForFirstGlyphAtLineStart()
		{
			QVERIFY(!MiniWindowUtils::runNeedsWrap(5, 16, 0, 5, 20));
		}

		void hasActivatableActionRequiresNonNoneAndNonBlankPayload()
		{
			QVERIFY(!MiniWindowUtils::hasActivatableAction(0, QStringLiteral("say hi"), 0));
			QVERIFY(!MiniWindowUtils::hasActivatableAction(1, QStringLiteral("   "), 0));
			QVERIFY(MiniWindowUtils::hasActivatableAction(1, QStringLiteral("say hi"), 0));
		}

		void colorFromRefRoundTripsRgbTriplet()
		{
			const QColor color = MiniWindowUtils::colorFromRef(0x563412);
			QCOMPARE(color.red(), 0x12);
			QCOMPARE(color.green(), 0x34);
			QCOMPARE(color.blue(), 0x56);
		}

		void colorFromRefOrTransparentHandlesMinusOneAsTransparent()
		{
			const QColor color = MiniWindowUtils::colorFromRefOrTransparent(-1);
			QCOMPARE(color.alpha(), 0);
		}

		void positionAndResizeUpdateCanonicalGeometryState()
		{
			MiniWindow window;
			MiniWindowUtils::create(window, QStringLiteral("geom"), 5, 7, 32, 24, 0, 0, QColor(Qt::black),
			                        QString());
			window.surface.fill(QColor(Qt::red));

			MiniWindowUtils::position(window, 40, 50, 4, kMiniWindowAbsoluteLocation);
			MiniWindowUtils::resize(window, 48, 36, 0x000000);

			QCOMPARE(window.location, QPoint(40, 50));
			QCOMPARE(window.position, 4);
			QCOMPARE(window.flags, kMiniWindowAbsoluteLocation);
			QCOMPARE(window.width, 48);
			QCOMPARE(window.height, 36);
			QCOMPARE(window.surface.size(), QSize(48, 36));
			QCOMPARE(window.surface.pixelColor(0, 0), QColor(Qt::red));
		}

		void detachedImageCopyPreservesCanonicalGeometryState()
		{
			MiniWindow window;
			MiniWindowUtils::create(window, QStringLiteral("geom-copy"), 5, 7, 32, 24, 0, 0,
			                        QColor(Qt::black), QStringLiteral("plugin-id"));
			MiniWindowUtils::position(window, 44, 55, 4, kMiniWindowAbsoluteLocation);
			MiniWindowUtils::resize(window, 64, 40, 0x000000);
			window.rect = QRect(window.location, QSize(window.width, window.height));
			window.surface.fill(QColor(Qt::blue));

			const MiniWindow copy = window.detachedImageCopy();

			QCOMPARE(copy.name, window.name);
			QCOMPARE(copy.location, QPoint(44, 55));
			QCOMPARE(copy.position, 4);
			QCOMPARE(copy.flags, kMiniWindowAbsoluteLocation);
			QCOMPARE(copy.width, 64);
			QCOMPARE(copy.height, 40);
			QCOMPARE(copy.rect, QRect(44, 55, 64, 40));
			QCOMPARE(copy.surface.size(), QSize(64, 40));
			QCOMPARE(copy.surface.pixelColor(0, 0), QColor(Qt::blue));
			QVERIFY(copy.surface.constBits() != window.surface.constBits());
		}

		void detachedImageCopySurvivesSourceMutationAndClear()
		{
			MiniWindow window;
			MiniWindowUtils::create(window, QStringLiteral("snapshot-copy"), 1, 2, 16, 12, 0, 0,
			                        QColor(Qt::black), QStringLiteral("plugin-id"));
			window.surface.fill(QColor(Qt::green));
			MiniWindowImage image;
			image.image = QImage(8, 6, QImage::Format_ARGB32_Premultiplied);
			image.image.fill(QColor(Qt::yellow));
			window.images.insert(QStringLiteral("img"), image);

			const MiniWindow copy = window.detachedImageCopy();

			window.surface.fill(QColor(Qt::red));
			window.images.clear();
			window.width  = 0;
			window.height = 0;

			QCOMPARE(copy.width, 16);
			QCOMPARE(copy.height, 12);
			QCOMPARE(copy.surface.pixelColor(0, 0), QColor(Qt::green));
			QVERIFY(copy.images.contains(QStringLiteral("img")));
			QCOMPARE(copy.images.value(QStringLiteral("img")).image.pixelColor(0, 0), QColor(Qt::yellow));
		}

		void bezierRejectsLegacyInvalidPointList()
		{
			MiniWindow window;
			MiniWindowUtils::create(window, QStringLiteral("test"), 0, 0, 32, 32, 0, 0, QColor(Qt::black),
			                        QString());

			QCOMPARE(MiniWindowUtils::bezier(window, QStringLiteral("0,0,,10,10,20,20,30,30"), 0, 0, 1),
			         eInvalidNumberOfPoints);
			QCOMPARE(MiniWindowUtils::bezier(window, QStringLiteral("0,0,10.5,10,20,20,30,30"), 0, 0, 1),
			         eInvalidPoint);
		}

		void polygonRejectsLegacyInvalidPointList()
		{
			MiniWindow window;
			MiniWindowUtils::create(window, QStringLiteral("test"), 0, 0, 32, 32, 0, 0, QColor(Qt::black),
			                        QString());

			QCOMPARE(
			    MiniWindowUtils::polygon(window, QStringLiteral("0,0,,10,20,20"), 0, 0, 1, 0, 0, true, false),
			    eInvalidPoint);
			QCOMPARE(
			    MiniWindowUtils::polygon(window, QStringLiteral("0,0,10e1,10"), 0, 0, 1, 0, 0, true, false),
			    eInvalidPoint);
		}
		// NOLINTEND(readability-convert-member-functions-to-static)
};

QTEST_APPLESS_MAIN(tst_MiniWindowUtils)

#if __has_include("tst_MiniWindowUtils.moc")
#include "tst_MiniWindowUtils.moc"
#endif
