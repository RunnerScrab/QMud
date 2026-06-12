/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: tst_OutputWrapUtils.cpp
 * Role: QTest coverage for fixed-column output wrapping behavior.
 */

#include "OutputWrapUtils.h"

#include <QColor>
#include <QtTest/QTest>

#include <limits>

namespace
{
	int safeQSizeToInt(const qsizetype size)
	{
		if (size <= 0)
			return 0;
		constexpr qsizetype kMaxInt = std::numeric_limits<int>::max();
		return size > kMaxInt ? std::numeric_limits<int>::max() : static_cast<int>(size);
	}

	WorldRuntime::StyleSpan makeStyleSpan(const int length, const QColor &fore = QColor(Qt::red))
	{
		WorldRuntime::StyleSpan span;
		span.length = length;
		span.fore   = fore;
		span.back   = QColor(Qt::black);
		span.bold   = true;
		return span;
	}
} // namespace

/**
 * @brief QTest fixture covering QMud fixed-column output wrapping scenarios.
 */
class tst_OutputWrapUtils : public QObject
{
		Q_OBJECT

		// NOLINTBEGIN(readability-convert-member-functions-to-static)
	private slots:
		void explicitNewlineBeforeWrapColumnIsPreserved()
		{
			QString text = QStringLiteral("alpha\nbeta gamma");

			QMudOutputWrapUtils::wrapPlainLineForColumn(text, 12, false);

			QCOMPARE(text, QStringLiteral("alpha\nbeta gamma"));
		}

		void wrapColumnStillAppliesBeforeLaterNewline()
		{
			QString text = QStringLiteral("alpha beta gamma\n");

			QMudOutputWrapUtils::wrapPlainLineForColumn(text, 12, false);

			QCOMPARE(text, QStringLiteral("alpha beta \ngamma\n"));
		}

		void exactWrapColumnDoesNotWrap()
		{
			QString text = QStringLiteral("1234567890");

			QMudOutputWrapUtils::wrapPlainLineForColumn(text, 10, false);

			QCOMPARE(text, QStringLiteral("1234567890"));
		}

		void onePastWrapColumnWraps()
		{
			QString text = QStringLiteral("12345678901");

			QMudOutputWrapUtils::wrapPlainLineForColumn(text, 10, false);

			QCOMPARE(text, QStringLiteral("1234567890\n1"));
		}

		void explicitNewlineAlsoStartsFreshWrapSegment()
		{
			QString text = QStringLiteral("alpha\nbeta gamma delta");

			QMudOutputWrapUtils::wrapPlainLineForColumn(text, 12, false);

			QCOMPARE(text, QStringLiteral("alpha\nbeta gamma \ndelta"));
		}

		void trailingColumnWidthRespectsFinalHardBreak()
		{
			QCOMPARE(QMudOutputWrapUtils::trailingLineColumnWidthForWrap(QStringLiteral("prompt> ")), 8);
			QCOMPARE(QMudOutputWrapUtils::trailingLineColumnWidthForWrap(QStringLiteral("line\nprompt> ")),
			         8);
		}

		void firstLinePrefixContributesToWrapWidth()
		{
			QString text = QStringLiteral("alpha beta");

			QMudOutputWrapUtils::wrapPlainLineForColumn(text, 12, false, 6);

			QCOMPARE(text, QStringLiteral("alpha \nbeta"));
		}

		void styledWrapPreservesInsertedBreakStyle()
		{
			QString                          text = QStringLiteral("alpha beta gamma\n");
			QVector<WorldRuntime::StyleSpan> spans{makeStyleSpan(safeQSizeToInt(text.size()))};

			QMudOutputWrapUtils::wrapStyledLineForColumn(text, spans, 12, false);

			QCOMPARE(text, QStringLiteral("alpha beta \ngamma\n"));
			QCOMPARE(spans.size(), 1);
			QCOMPARE(spans.first().length, safeQSizeToInt(text.size()));
			QCOMPARE(spans.first().fore, QColor(Qt::red));
			QCOMPARE(spans.first().back, QColor(Qt::black));
			QVERIFY(spans.first().bold);
		}

		void splitCrLfCreatesHardReturnBoundary()
		{
			const QVector<QMudOutputWrapUtils::OutputLineSegment> segments =
			    QMudOutputWrapUtils::splitOutputTextAtLineBreaks(QStringLiteral("alpha\r\nbeta"), {}, false);

			QCOMPARE(segments.size(), 2);
			QCOMPARE(segments.at(0).text, QStringLiteral("alpha"));
			QVERIFY(segments.at(0).hardReturn);
			QCOMPARE(segments.at(1).text, QStringLiteral("beta"));
			QVERIFY(!segments.at(1).hardReturn);
		}

		void splitTrailingNewlineDoesNotAddExtraSegment()
		{
			const QVector<QMudOutputWrapUtils::OutputLineSegment> segments =
			    QMudOutputWrapUtils::splitOutputTextAtLineBreaks(QStringLiteral("alpha\n"), {}, true);

			QCOMPARE(segments.size(), 1);
			QCOMPARE(segments.at(0).text, QStringLiteral("alpha"));
			QVERIFY(segments.at(0).hardReturn);
		}

		void splitStandaloneCrLfKeepsBlankHardLine()
		{
			const QVector<QMudOutputWrapUtils::OutputLineSegment> segments =
			    QMudOutputWrapUtils::splitOutputTextAtLineBreaks(QStringLiteral("\r\n"), {}, false);

			QCOMPARE(segments.size(), 1);
			QVERIFY(segments.at(0).text.isEmpty());
			QVERIFY(segments.at(0).hardReturn);
		}

		void splitBareCarriageReturnCreatesHardReturnBoundary()
		{
			const QVector<QMudOutputWrapUtils::OutputLineSegment> segments =
			    QMudOutputWrapUtils::splitOutputTextAtLineBreaks(QStringLiteral("alpha\rbeta"), {}, true);

			QCOMPARE(segments.size(), 2);
			QCOMPARE(segments.at(0).text, QStringLiteral("alpha"));
			QVERIFY(segments.at(0).hardReturn);
			QCOMPARE(segments.at(1).text, QStringLiteral("beta"));
			QVERIFY(segments.at(1).hardReturn);
		}

		void splitMixedPluginFragmentKeepsNextTextOpen()
		{
			const QVector<QMudOutputWrapUtils::OutputLineSegment> segments =
			    QMudOutputWrapUtils::splitOutputTextAtLineBreaks(QStringLiteral("room header\r\nW"), {},
			                                                     false);

			QCOMPARE(segments.size(), 2);
			QCOMPARE(segments.at(0).text, QStringLiteral("room header"));
			QVERIFY(segments.at(0).hardReturn);
			QCOMPARE(segments.at(1).text, QStringLiteral("W"));
			QVERIFY(!segments.at(1).hardReturn);
		}

		void wrappedBreaksSplitIntoRuntimeHardLines()
		{
			QString text = QStringLiteral("alpha beta gamma");
			QMudOutputWrapUtils::wrapPlainLineForColumn(text, 12, false);

			const QVector<QMudOutputWrapUtils::OutputLineSegment> segments =
			    QMudOutputWrapUtils::splitOutputTextAtLineBreaks(text, {}, true);

			QCOMPARE(segments.size(), 2);
			QCOMPARE(segments.at(0).text, QStringLiteral("alpha beta "));
			QVERIFY(segments.at(0).hardReturn);
			QCOMPARE(segments.at(1).text, QStringLiteral("gamma"));
			QVERIFY(segments.at(1).hardReturn);
		}

		void splitStyledTextPreservesSpanSlices()
		{
			const QString                                         text = QStringLiteral("red\r\nblue");
			const QVector<WorldRuntime::StyleSpan>                spans{makeStyleSpan(3, QColor(Qt::red)),
			                                                            makeStyleSpan(2, QColor(Qt::yellow)),
			                                                            makeStyleSpan(4, QColor(Qt::green))};

			const QVector<QMudOutputWrapUtils::OutputLineSegment> segments =
			    QMudOutputWrapUtils::splitOutputTextAtLineBreaks(text, spans, false);

			QCOMPARE(segments.size(), 2);
			QCOMPARE(segments.at(0).text, QStringLiteral("red"));
			QCOMPARE(segments.at(0).spans.size(), 1);
			QCOMPARE(segments.at(0).spans.first().length, 3);
			QCOMPARE(segments.at(0).spans.first().fore, QColor(Qt::red));
			QCOMPARE(segments.at(1).text, QStringLiteral("blue"));
			QCOMPARE(segments.at(1).spans.size(), 1);
			QCOMPARE(segments.at(1).spans.first().length, 4);
			QCOMPARE(segments.at(1).spans.first().fore, QColor(Qt::green));
			QVERIFY(!segments.at(1).hardReturn);
		}

		void localOutputWrapConfigUsesWorldColumnWithoutNaws()
		{
			QMap<QString, QString> attrs;
			attrs.insert(QStringLiteral("wrap"), QStringLiteral("1"));
			attrs.insert(QStringLiteral("wrap_column"), QStringLiteral("127"));
			attrs.insert(QStringLiteral("indent_paras"), QStringLiteral("0"));
			attrs.insert(QStringLiteral("auto_wrap_window_width"), QStringLiteral("0"));

			const QMudOutputWrapUtils::FixedColumnWrapConfig config =
			    QMudOutputWrapUtils::localOutputWrapConfig(attrs, false, 80);

			QVERIFY(config.enabled);
			QCOMPARE(config.wrapColumn, 127);
			QVERIFY(!config.indentParas);
		}

		void localOutputWrapConfigUsesWindowColumnWhenAutomatic()
		{
			QMap<QString, QString> attrs;
			attrs.insert(QStringLiteral("wrap"), QStringLiteral("1"));
			attrs.insert(QStringLiteral("wrap_column"), QStringLiteral("70"));
			attrs.insert(QStringLiteral("auto_wrap_window_width"), QStringLiteral("1"));

			const QMudOutputWrapUtils::FixedColumnWrapConfig config =
			    QMudOutputWrapUtils::localOutputWrapConfig(attrs, false, 80);

			QVERIFY(config.enabled);
			QCOMPARE(config.wrapColumn, 80);
		}
		// NOLINTEND(readability-convert-member-functions-to-static)
};

QTEST_APPLESS_MAIN(tst_OutputWrapUtils)

#if __has_include("tst_OutputWrapUtils.moc")
#include "tst_OutputWrapUtils.moc"
#endif
