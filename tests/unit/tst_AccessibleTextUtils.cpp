/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: tst_AccessibleTextUtils.cpp
 * Role: QTest coverage for accessible text offset mapping helpers.
 */

#include "AccessibleTextUtils.h"

#include <QtTest/QTest>

using QMudAccessibleTextUtils::LineOffsetMap;
using QMudAccessibleTextUtils::TextPosition;

/**
 * @brief QTest fixture covering native output accessible text mapping.
 */
class tst_AccessibleTextUtils : public QObject
{
		Q_OBJECT

		// NOLINTBEGIN(readability-convert-member-functions-to-static)
	private slots:
		void emptyOutputHasNoAccessibleText()
		{
			const LineOffsetMap map;

			QVERIFY(map.isEmpty());
			QCOMPARE(map.lineCount(), 0);
			QCOMPARE(map.characterCount(), 0);
			QCOMPARE(map.text(0, 10), QString());
			QCOMPARE(map.offsetForPosition({3, 7}), 0);

			const TextPosition position = map.positionForOffset(5);
			QCOMPARE(position.line, 0);
			QCOMPARE(position.column, 0);
		}

		void oneLineMapsOffsetsWithoutTrailingNewline()
		{
			const LineOffsetMap map({QStringLiteral("prompt> look")});

			QVERIFY(!map.isEmpty());
			QCOMPARE(map.lineCount(), 1);
			QCOMPARE(map.characterCount(), 12);
			QCOMPARE(map.text(0, map.characterCount()), QStringLiteral("prompt> look"));
			QCOMPARE(map.offsetForPosition({0, 8}), 8);

			const TextPosition position = map.positionForOffset(8);
			QCOMPARE(position.line, 0);
			QCOMPARE(position.column, 8);
		}

		void multipleLinesUseNewlineSeparators()
		{
			const LineOffsetMap map(
			    {QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")});

			QCOMPARE(map.characterCount(), 16);
			QCOMPARE(map.text(0, map.characterCount()), QStringLiteral("alpha\nbeta\ngamma"));
			QCOMPARE(map.offsetForPosition({1, 0}), 6);
			QCOMPARE(map.offsetForPosition({2, 2}), 13);

			const TextPosition newlinePosition = map.positionForOffset(5);
			QCOMPARE(newlinePosition.line, 0);
			QCOMPARE(newlinePosition.column, 5);
		}

		void slicesCanStartAndEndInsideDifferentLines()
		{
			const LineOffsetMap map(
			    {QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")});

			QCOMPARE(map.text(2, 12), QStringLiteral("pha\nbeta\ng"));
			QCOMPARE(map.text(5, 6), QStringLiteral("\n"));
			QCOMPARE(map.text(6, 10), QStringLiteral("beta"));
		}

		void positionsAndOffsetsClampToCurrentBuffer()
		{
			const LineOffsetMap map({QStringLiteral("one"), QStringLiteral("two")});

			QCOMPARE(map.offsetForPosition({-2, -5}), 0);
			QCOMPARE(map.offsetForPosition({0, 99}), 3);
			QCOMPARE(map.offsetForPosition({7, 1}), map.characterCount());

			TextPosition position = map.positionForOffset(-10);
			QCOMPARE(position.line, 0);
			QCOMPARE(position.column, 0);

			position = map.positionForOffset(999);
			QCOMPARE(position.line, 1);
			QCOMPARE(position.column, 3);
		}

		void resetModelsCappedBufferTrim()
		{
			LineOffsetMap map({QStringLiteral("old1"), QStringLiteral("old2"), QStringLiteral("new")});
			QCOMPARE(map.text(0, map.characterCount()), QStringLiteral("old1\nold2\nnew"));

			map.reset({QStringLiteral("old2"), QStringLiteral("new"), QStringLiteral("tail")});

			QCOMPARE(map.characterCount(), 13);
			QCOMPARE(map.text(0, map.characterCount()), QStringLiteral("old2\nnew\ntail"));
			QCOMPARE(map.offsetForPosition({1, 0}), 5);
		}

		void selectionTextUsesEitherEndpointOrder()
		{
			const LineOffsetMap map(
			    {QStringLiteral("north"), QStringLiteral("east"), QStringLiteral("south")});

			constexpr TextPosition anchor{0, 2};
			constexpr TextPosition cursor{2, 3};

			QCOMPARE(map.selectedText(anchor, cursor), QStringLiteral("rth\neast\nsou"));
			QCOMPARE(map.selectedText(cursor, anchor), QStringLiteral("rth\neast\nsou"));
			QCOMPARE(map.selectedText({1, 1}, {1, 1}), QString());
		}
		// NOLINTEND(readability-convert-member-functions-to-static)
};

QTEST_APPLESS_MAIN(tst_AccessibleTextUtils)

#if __has_include("tst_AccessibleTextUtils.moc")
#include "tst_AccessibleTextUtils.moc"
#endif
