/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: tst_LuaBridgeDispatcherIntegration.cpp
 * Role: Integration coverage for Lua bridge dispatcher timeout and cancellation behavior.
 */

#include "helpers/LuaExecutionUtils.h"

// ReSharper disable once CppUnusedIncludeDirective
#include <QCoreApplication>
#include <QElapsedTimer>
// ReSharper disable once CppUnusedIncludeDirective
#include <QFile>
#include <QMetaObject>
// ReSharper disable once CppUnusedIncludeDirective
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
// ReSharper disable once CppUnusedIncludeDirective
#include <QTemporaryDir>
#include <QTextStream>
// ReSharper disable once CppUnusedIncludeDirective
#include <QThread>
#include <QtTest/QTest>

#include <atomic>

namespace
{
	QString          g_fatalLogPath;
	QtMessageHandler g_prevMessageHandler{nullptr};

	void             bridgeFatalChildMessageHandler(const QtMsgType type, const QMessageLogContext &context,
	                                                const QString &message)
	{
		if (!g_fatalLogPath.isEmpty())
		{
			QFile file(g_fatalLogPath);
			if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
			{
				QTextStream stream(&file);
				stream << message << '\n';
			}
		}
		if (g_prevMessageHandler)
			g_prevMessageHandler(type, context, message);
	}

	int runBridgeInFlightTimeoutChild()
	{
		g_fatalLogPath = qEnvironmentVariable("QMUD_BRIDGE_FATAL_LOG");
		if (!g_fatalLogPath.isEmpty())
			g_prevMessageHandler = qInstallMessageHandler(bridgeFatalChildMessageHandler);

		QThread workerThread;
		workerThread.setObjectName(QStringLiteral("tst_LuaBridgeFatalChildWorker"));
		QObject  target;
		QThread *mainThread = QThread::currentThread();
		target.moveToThread(&workerThread);
		workerThread.start();
		const auto stopWorker = qScopeGuard(
		    [&]()
		    {
			    if (target.thread() == &workerThread)
			    {
				    static_cast<void>(qmudLuaBridgeInvokeOnObjectThread(
				        &target, [&target, mainThread] { target.moveToThread(mainThread); }));
			    }
			    workerThread.quit();
			    static_cast<void>(workerThread.wait());
		    });

		if (!qmudLuaBridgeEnsureObjectThreadReady(&target))
		{
			qWarning().noquote()
			    << QStringLiteral("Fatal-timeout child setup failed: %1").arg(qmudLuaBridgeLastError());
			return 20;
		}

		if (qmudLuaBridgeInvokeOnObjectThread(&target, []() { QThread::sleep(31); }))
		{
			qWarning().noquote() << QStringLiteral(
			    "Expected in-flight timeout failure in child path, but invoke unexpectedly succeeded");
			return 21;
		}
		if (qmudLuaBridgeLastStatus() != LuaBridgeInvokeStatus::TimedOutInFlight)
		{
			qWarning().noquote() << QStringLiteral("Unexpected in-flight timeout status: %1")
			                            .arg(static_cast<int>(qmudLuaBridgeLastStatus()));
			return 23;
		}
		const QString error = qmudLuaBridgeLastError();
		if (!error.contains(
		        QStringLiteral("Invoke timed out after 30000 ms while request was already executing")))
		{
			qWarning().noquote() << QStringLiteral("Unexpected in-flight timeout error: %1").arg(error);
			return 22;
		}
		return 0;
	}
} // namespace

/**
 * @brief QTest fixture covering bridge dispatcher timeout behavior.
 */
class tst_LuaBridgeDispatcherIntegration : public QObject
{
		Q_OBJECT

		// NOLINTBEGIN(readability-convert-member-functions-to-static)
	private slots:
		void sameThreadInvokeRunsInlineAndSucceeds()
		{
			QObject          target;
			std::atomic_bool invoked{false};
			QVERIFY(qmudLuaBridgeInvokeOnObjectThread(&target, [&]() { invoked.store(true); }));
			QVERIFY(invoked.load());
			QCOMPARE(qmudLuaBridgeLastStatus(), LuaBridgeInvokeStatus::Success);
		}

		void reentrantBridgeCycleCompletes()
		{
			QThread workerThread;
			workerThread.setObjectName(QStringLiteral("tst_LuaBridgeReentrantWorker"));
			QObject  workerTarget;
			QObject  mainTarget;
			QThread *mainThread = QThread::currentThread();
			workerTarget.moveToThread(&workerThread);
			workerThread.start();
			const auto stopWorker = qScopeGuard(
			    [&]()
			    {
				    if (workerTarget.thread() == &workerThread)
				    {
					    static_cast<void>(
					        qmudLuaBridgeInvokeOnObjectThread(&workerTarget, [&workerTarget, mainThread]
					                                          { workerTarget.moveToThread(mainThread); }));
				    }
				    workerThread.quit();
				    static_cast<void>(workerThread.wait());
			    });

			QVERIFY(qmudLuaBridgeEnsureObjectThreadReady(&workerTarget));
			QVERIFY(qmudLuaBridgeEnsureObjectThreadReady(&mainTarget));

			std::atomic_bool workerCallbackInvoked{false};
			std::atomic_bool mainCallbackInvoked{false};
			const bool       outerOk = qmudLuaBridgeInvokeOnObjectThread(
                &workerTarget,
                [&]()
                {
                    const bool innerOk = qmudLuaBridgeInvokeOnObjectThread(
                        &mainTarget, [&]() { mainCallbackInvoked.store(true); });
                    if (innerOk)
                        workerCallbackInvoked.store(true);
                });

			QVERIFY(outerOk);
			QVERIFY(workerCallbackInvoked.load());
			QVERIFY(mainCallbackInvoked.load());
			QCOMPARE(qmudLuaBridgeLastStatus(), LuaBridgeInvokeStatus::Success);
		}

		void queuedInvokeTimeoutCancelsRequest()
		{
			QThread workerThread;
			workerThread.setObjectName(QStringLiteral("tst_LuaBridgeTimeoutWorker"));
			QObject  target;
			QObject  blocker;
			QThread *mainThread = QThread::currentThread();
			target.moveToThread(&workerThread);
			blocker.moveToThread(&workerThread);
			workerThread.start();
			const auto stopWorker = qScopeGuard(
			    [&]()
			    {
				    QElapsedTimer deadline;
				    deadline.start();
				    while (target.thread() == &workerThread && deadline.elapsed() < 5000)
				    {
					    if (qmudLuaBridgeInvokeOnObjectThread(&target, [&target, mainThread]
					                                          { target.moveToThread(mainThread); }))
					    {
						    break;
					    }
					    QThread::msleep(25);
				    }
				    workerThread.quit();
				    static_cast<void>(workerThread.wait());
			    });

			QVERIFY(qmudLuaBridgeEnsureObjectThreadReady(&target));

			std::atomic_bool blockerEntered{false};
			QVERIFY(QMetaObject::invokeMethod(
			    &blocker,
			    [&]()
			    {
				    blockerEntered.store(true);
				    QThread::sleep(31);
			    },
			    Qt::QueuedConnection));
			QTRY_VERIFY_WITH_TIMEOUT(blockerEntered.load(), 2000);

			std::atomic_bool callbackInvoked{false};
			QVERIFY(!qmudLuaBridgeInvokeOnObjectThread(&target, [&]() { callbackInvoked.store(true); }));
			QCOMPARE(qmudLuaBridgeLastStatus(), LuaBridgeInvokeStatus::RequestCanceledBeforeDispatch);
			QVERIFY(
			    qmudLuaBridgeLastError().contains(QStringLiteral("timed out and canceled queued request")));
			QVERIFY(!callbackInvoked.load());
		}

		void inFlightRequestTimeoutReturnsFailure()
		{
			QTemporaryDir tempDir;
			QVERIFY(tempDir.isValid());
			const QString logPath = tempDir.filePath(QStringLiteral("bridge_fatal.log"));

			QProcess      process;
			process.setProgram(QCoreApplication::applicationFilePath());
			process.setArguments({QStringLiteral("--bridge-inflight-timeout-child")});
			QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
			env.insert(QStringLiteral("QMUD_BRIDGE_FATAL_LOG"), logPath);
			process.setProcessEnvironment(env);
			process.start();
			QVERIFY(process.waitForStarted());
			QVERIFY(process.waitForFinished(70000));

			QFile      logFile(logPath);
			QByteArray fatalLog;
			if (logFile.open(QIODevice::ReadOnly | QIODevice::Text))
				fatalLog = logFile.readAll();

			const QByteArray output =
			    process.readAllStandardError() + process.readAllStandardOutput() + fatalLog;
			QCOMPARE(process.exitStatus(), QProcess::NormalExit);
			QCOMPARE(process.exitCode(), 0);
			QVERIFY(output.contains("Invoke timed out after 30000 ms while request was already executing"));
		}
		// NOLINTEND(readability-convert-member-functions-to-static)
};

int main(int argc, char **argv)
{
	QCoreApplication  app(argc, argv);
	const QStringList args = QCoreApplication::arguments();
	if (args.contains(QStringLiteral("--bridge-inflight-timeout-child")))
		return runBridgeInFlightTimeoutChild();

	tst_LuaBridgeDispatcherIntegration test;
	return QTest::qExec(&test, argc, argv);
}

#if __has_include("tst_LuaBridgeDispatcherIntegration.moc")
#include "tst_LuaBridgeDispatcherIntegration.moc"
#endif
