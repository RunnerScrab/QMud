/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: tst_WorldCommandProcessor_Trigger.cpp
 * Role: Runtime-backed QTest coverage for WorldCommandProcessor trigger evaluation behavior.
 */

#include "WorldCommandProcessor.h"

#include "WorldCommandProcessorUtils.h"
#include "WorldOptions.h"

#include <QtTest/QTest>

namespace
{
	/**
	 * @brief Creates a two-line regexp trigger that captures trailing whitespace.
	 * @return Trigger configured to store captures in a world variable.
	 */
	WorldRuntime::Trigger makeMultilineWhitespaceTrigger()
	{
		WorldRuntime::Trigger trigger;
		trigger.attributes.insert(QStringLiteral("enabled"), QStringLiteral("y"));
		trigger.attributes.insert(QStringLiteral("match"), QStringLiteral("^(.*) (\\d+) (.*)\\n(.*?)\\n$"));
		trigger.attributes.insert(QStringLiteral("regexp"), QStringLiteral("y"));
		trigger.attributes.insert(QStringLiteral("multi_line"), QStringLiteral("y"));
		trigger.attributes.insert(QStringLiteral("lines_to_match"), QStringLiteral("2"));
		trigger.attributes.insert(QStringLiteral("send_to"), QString::number(eSendToVariable));
		trigger.attributes.insert(QStringLiteral("variable"), QStringLiteral("captured_multiline"));
		trigger.attributes.insert(QStringLiteral("sequence"), QStringLiteral("100"));
		trigger.children.insert(QStringLiteral("send"), QStringLiteral("%1|%2|%3|%4"));
		return trigger;
	}
} // namespace

/**
 * @brief QTest fixture covering real WorldCommandProcessor trigger evaluation.
 */
class tst_WorldCommandProcessor_Trigger : public QObject
{
		Q_OBJECT

	private slots:
		static void triggerMatchTargetPreservesTrailingWhitespaceForRegexp()
		{
			const QString line = QStringLiteral("<274hp 930sp 448st> ");
			QCOMPARE(QMudCommandText::normalizeTriggerMatchLine(line, true), line);
		}

		static void triggerMatchTargetTrimsTrailingWhitespaceForWildcard()
		{
			const QString line = QStringLiteral("<274hp 930sp 448st> \t");
			QCOMPARE(QMudCommandText::normalizeTriggerMatchLine(line, false),
			         QStringLiteral("<274hp 930sp 448st>"));
		}

		static void triggerMultilineTargetPreservesTrailingWhitespaceForRegexp()
		{
			const QStringList lines = {QStringLiteral("line 1 "), QStringLiteral("line 2\t")};
			QCOMPARE(QMudCommandText::buildTriggerMultilineTarget(lines, true),
			         QStringLiteral("line 1 \nline 2\t\n"));
		}

		static void triggerMultilineTargetTrimsTrailingWhitespaceForWildcard()
		{
			const QStringList lines = {QStringLiteral("line 1 "), QStringLiteral("line 2\t")};
			QCOMPARE(QMudCommandText::buildTriggerMultilineTarget(lines, false),
			         QStringLiteral("line 1\nline 2\n"));
		}

		static void pluginTriggerSoundBypassesWorldToggle()
		{
			QVERIFY(QMudTriggerSound::shouldPlayTriggerSound(true, false));
			QVERIFY(QMudTriggerSound::shouldPlayTriggerSound(true, true));
		}

		static void worldTriggerSoundFollowsWorldToggle()
		{
			QVERIFY(!QMudTriggerSound::shouldPlayTriggerSound(false, false));
			QVERIFY(QMudTriggerSound::shouldPlayTriggerSound(false, true));
		}

		static void multilineRegexpTriggerPreservesTrailingWhitespaceInProcessor()
		{
			WorldRuntime runtime;
			runtime.setWorldAttribute(QStringLiteral("enable_triggers"), QStringLiteral("y"));
			runtime.setWorldAttribute(QStringLiteral("enable_trigger_sounds"), QStringLiteral("n"));
			runtime.setWorldAttribute(QStringLiteral("script_language"), QStringLiteral("Lua"));
			runtime.triggersMutable().push_back(makeMultilineWhitespaceTrigger());

			WorldCommandProcessor processor;
			processor.setRuntime(&runtime);
			processor.onIncomingLineReceived(QStringLiteral("Multi Line:  1 Trigger "));
			processor.onIncomingLineReceived(QStringLiteral("Expires on Sat Jul  4 18:08:48 2026."));

			QString captured;
			QVERIFY(runtime.findVariable(QStringLiteral("captured_multiline"), captured));
			QCOMPARE(captured,
			         QStringLiteral("Multi Line: |1|Trigger |Expires on Sat Jul  4 18:08:48 2026."));
			QCOMPARE(runtime.triggers().constFirst().matched, 1);
		}
};

QTEST_MAIN(tst_WorldCommandProcessor_Trigger)

#if __has_include("tst_WorldCommandProcessor_Trigger.moc")
#include "tst_WorldCommandProcessor_Trigger.moc"
#endif
