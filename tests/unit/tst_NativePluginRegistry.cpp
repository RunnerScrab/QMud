/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: tst_NativePluginRegistry.cpp
 * Role: Unit coverage for native compatibility plugin registry behavior.
 */

#include "NativePluginRegistry.h"
#include "TelnetProcessor.h"
#include "WorldDocument.h"
#include "WorldOptions.h"
#include "WorldRuntime.h"
#include "scripting/ScriptingErrors.h"

// ReSharper disable once CppUnusedIncludeDirective
#include <QDir>
#include <QFile>
#include <QFileInfo>
// ReSharper disable once CppUnusedIncludeDirective
#include <QTemporaryDir>
#include <QtTest/QTest>

namespace
{
	struct RuntimeStubState
	{
			QString             startupDirectory;
			QStringList         outputLines;
			int                 nextAcceleratorCommand{WorldRuntime::kAcceleratorFirstCommand};
			QHash<qint64, int>  acceleratorKeyToCommand;
			QHash<int, QString> acceleratorTextByCommand;
			QHash<int, int>     acceleratorSendToByCommand;
			QHash<int, QString> acceleratorPluginByCommand;
	};

	QHash<const WorldRuntime *, RuntimeStubState> &runtimeStates()
	{
		static QHash<const WorldRuntime *, RuntimeStubState> states;
		return states;
	}

	RuntimeStubState &stateFor(const WorldRuntime *runtime)
	{
		return runtimeStates()[runtime];
	}

	QString writePluginXml(const QTemporaryDir &dir, const QString &id, const QString &script)
	{
		const QString path = dir.filePath(QStringLiteral("%1.xml").arg(id));
		QFile         file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
			return {};
		const QString xml = QStringLiteral(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<muclient>
  <plugin name="Plugin" author="Tester" id="%1" language="lua" purpose="test">
    <description>test plugin</description>
    <script>%2</script>
  </plugin>
</muclient>
)xml")
		                        .arg(id, script);
		file.write(xml.toUtf8());
		file.close();
		return path;
	}
} // namespace

// NOLINTBEGIN(readability-convert-member-functions-to-static,readability-make-member-function-const)
TelnetProcessor::TelnetProcessor() = default;

WorldRuntime::WorldRuntime(QObject *parent) : QObject(parent)
{
	RuntimeStubState state;
	state.startupDirectory = QDir::currentPath();
	runtimeStates().insert(this, state);
}

WorldRuntime::~WorldRuntime()
{
	QMudNativePluginRegistry::discardRuntimeState(this);
	runtimeStates().remove(this);
}

QString WorldRuntime::startupDirectory() const
{
	return stateFor(this).startupDirectory;
}

void WorldRuntime::outputText(const QString &text, bool, bool)
{
	stateFor(this).outputLines.push_back(text);
}

void WorldRuntime::notifyNativePluginStateChanged()
{
}

int WorldRuntime::allocateAcceleratorCommand()
{
	return stateFor(this).nextAcceleratorCommand++;
}

void WorldRuntime::registerAccelerator(const qint64 key, const int commandId, const AcceleratorEntry &entry)
{
	RuntimeStubState &state = stateFor(this);
	state.acceleratorKeyToCommand.insert(key, commandId);
	state.acceleratorTextByCommand.insert(commandId, entry.text);
	state.acceleratorSendToByCommand.insert(commandId, entry.sendTo);
	state.acceleratorPluginByCommand.insert(commandId, entry.pluginId);
}

int WorldRuntime::acceleratorCommandForKey(const qint64 key) const
{
	return stateFor(this).acceleratorKeyToCommand.value(key, -1);
}

QString WorldRuntime::acceleratorCommandText(const int commandId) const
{
	return stateFor(this).acceleratorTextByCommand.value(commandId);
}

int WorldRuntime::acceleratorSendTarget(const int commandId) const
{
	return stateFor(this).acceleratorSendToByCommand.value(commandId, -1);
}

QString WorldRuntime::acceleratorPluginId(const int commandId) const
{
	return stateFor(this).acceleratorPluginByCommand.value(commandId);
}
// NOLINTEND(readability-convert-member-functions-to-static,readability-make-member-function-const)

/**
 * @brief QTest fixture covering native plugin registry shim and blacklist rules.
 */
class tst_NativePluginRegistry : public QObject
{
		Q_OBJECT

		// NOLINTBEGIN(readability-convert-member-functions-to-static)
	private slots:
		void init()
		{
			QMudNativePluginRegistry::setTestSpeechSink({});
		}

		void cleanup()
		{
			QMudNativePluginRegistry::setTestSpeechSink({});
		}

		void shimMetadataAndRoutineSurface()
		{
			const QString shimId = QMudNativePluginRegistry::mushReaderPluginId();
			QVERIFY(QMudNativePluginRegistry::isShimId(shimId));
			QVERIFY(QMudNativePluginRegistry::isProtectedId(shimId));
			QCOMPARE(QMudNativePluginRegistry::resolveShimIdOrName(QStringLiteral("MushReader")), shimId);

			QMudNativePluginRegistry::NativePluginMetadata metadata;
			QVERIFY(QMudNativePluginRegistry::metadataForShim(shimId, metadata));
			QCOMPARE(metadata.id, shimId);
			QCOMPARE(metadata.name, QStringLiteral("MushReader"));
			QCOMPARE(QMudNativePluginRegistry::pluginInfo(shimId, 1).toString(),
			         QStringLiteral("MushReader"));
			QCOMPARE(QMudNativePluginRegistry::pluginInfo(shimId, 17).toBool(), false);
			QCOMPARE(QMudNativePluginRegistry::pluginInfo(shimId, 17, 0, true).toBool(), true);
			QCOMPARE(QMudNativePluginRegistry::pluginInfo(shimId, 21, 7).toInt(), 7);

			QCOMPARE(QMudNativePluginRegistry::pluginSupports(shimId, QStringLiteral("say")), eOK);
			QCOMPARE(QMudNativePluginRegistry::pluginSupports(shimId, QStringLiteral("interrupt")), eOK);
			QCOMPARE(QMudNativePluginRegistry::pluginSupports(shimId, QStringLiteral("stop")), eOK);
			QCOMPARE(QMudNativePluginRegistry::pluginSupports(shimId, QStringLiteral("plugin_update_url")),
			         eOK);
			QCOMPARE(QMudNativePluginRegistry::pluginSupports(shimId, QStringLiteral("missing")),
			         eNoSuchRoutine);
		}

		void callPluginRoutesToNativeSpeech()
		{
			WorldRuntime                                       runtime;
			QVector<QMudNativePluginRegistry::TestSpeechEvent> events;
			QMudNativePluginRegistry::setTestSpeechSink(
			    [&events](const QMudNativePluginRegistry::TestSpeechEvent &event)
			    { events.push_back(event); });

			const QString shimId = QMudNativePluginRegistry::mushReaderPluginId();
			QCOMPARE(QMudNativePluginRegistry::callRoutine(&runtime, shimId, QStringLiteral("say"),
			                                               {QStringLiteral("line")})
			             .errorCode,
			         eOK);
			QCOMPARE(QMudNativePluginRegistry::callRoutine(&runtime, shimId, QStringLiteral("interrupt"),
			                                               {QStringLiteral("urgent")})
			             .errorCode,
			         eOK);
			QCOMPARE(
			    QMudNativePluginRegistry::callRoutine(&runtime, shimId, QStringLiteral("stop"), {}).errorCode,
			    eOK);
			const QMudNativePluginRegistry::NativeCallResult update = QMudNativePluginRegistry::callRoutine(
			    &runtime, shimId, QStringLiteral("plugin_update_url"), {});
			QCOMPARE(update.errorCode, eOK);
			QCOMPARE(update.returnValues.size(), 1);
			QCOMPARE(update.returnValues.constFirst().toString(), QStringLiteral("qmud:native/MushReader"));

			QCOMPARE(events.size(), 3);
			QCOMPARE(events.at(0).text, QStringLiteral("       line"));
			QVERIFY(!events.at(0).interrupt);
			QCOMPARE(events.at(1).text, QStringLiteral("urgent"));
			QVERIFY(events.at(1).interrupt);
			QVERIFY(events.at(2).stop);
			QCOMPARE(QMudNativePluginRegistry::callRoutine(&runtime, shimId, QStringLiteral("missing"), {})
			             .errorCode,
			         eNoSuchRoutine);
		}

		void commandFallbackAndSubstitutionBehavior()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			WorldRuntime runtime;
			stateFor(&runtime).startupDirectory = dir.path();

			QVector<QMudNativePluginRegistry::TestSpeechEvent> events;
			QMudNativePluginRegistry::setTestSpeechSink(
			    [&events](const QMudNativePluginRegistry::TestSpeechEvent &event)
			    { events.push_back(event); });

			QVERIFY(QMudNativePluginRegistry::handleCommand(&runtime, QStringLiteral("tts_note hello")));
			QVERIFY(QMudNativePluginRegistry::handleCommand(&runtime, QStringLiteral("tts_interrupt now")));
			QVERIFY(QMudNativePluginRegistry::handleCommand(&runtime, QStringLiteral("tts_stop")));
			QVERIFY(QMudNativePluginRegistry::handleCommand(&runtime, QStringLiteral("MushReader help")));
			QVERIFY(QMudNativePluginRegistry::handleCommand(
			    &runtime, QStringLiteral("subst add source line==replacement line")));

			QCOMPARE(events.size(), 3);
			QCOMPARE(events.at(0).text, QStringLiteral("hello"));
			QVERIFY(!events.at(0).interrupt);
			QCOMPARE(events.at(1).text, QStringLiteral("now"));
			QVERIFY(events.at(1).interrupt);
			QVERIFY(events.at(2).stop);
			QVERIFY(stateFor(&runtime).outputLines.contains(QStringLiteral("MushReader native QMud shim.")));
			QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("substitutions.mush"))));

			events.clear();
			QMudNativePluginRegistry::handleScreenDraw(&runtime, 1, 0, QStringLiteral("source line"));
			QVERIFY(events.isEmpty());
			QVERIFY(QMudNativePluginRegistry::handleCommand(&runtime, QStringLiteral("tts")));
			QVERIFY(events.size() >= 2);
			QVERIFY(events.at(events.size() - 2).stop);
			QCOMPARE(events.constLast().text, QStringLiteral("speech on"));

			events.clear();
			QMudNativePluginRegistry::handleScreenDraw(&runtime, 1, 0, QStringLiteral("source line"));
			QCOMPARE(events.size(), 1);
			QCOMPARE(events.constFirst().text, QStringLiteral("replacement line"));

			QVERIFY(
			    QMudNativePluginRegistry::handleCommand(&runtime, QStringLiteral("subst add muted==!skip")));
			events.clear();
			QMudNativePluginRegistry::handleScreenDraw(&runtime, 1, 0, QStringLiteral("muted"));
			QVERIFY(events.isEmpty());

			QVERIFY(QMudNativePluginRegistry::handleCommand(&runtime, QStringLiteral("subst off")));
			events.clear();
			QMudNativePluginRegistry::handleScreenDraw(&runtime, 1, 0, QStringLiteral("source line"));
			QVERIFY(events.isEmpty());
		}

		void screenDrawTabCompleteAndToggleSpeech()
		{
			WorldRuntime                                       runtime;
			QVector<QMudNativePluginRegistry::TestSpeechEvent> events;
			QMudNativePluginRegistry::setTestSpeechSink(
			    [&events](const QMudNativePluginRegistry::TestSpeechEvent &event)
			    { events.push_back(event); });

			QMudNativePluginRegistry::handleScreenDraw(&runtime, 2, 0, QStringLiteral("ignored"));
			QVERIFY(events.isEmpty());
			QMudNativePluginRegistry::handleScreenDraw(&runtime, 1, 0, QStringLiteral("spoken"));
			QVERIFY(events.isEmpty());

			QMudNativePluginRegistry::handleTabComplete(&runtime, QStringLiteral("north"));
			QVERIFY(events.isEmpty());

			QVERIFY(QMudNativePluginRegistry::handleCommand(&runtime, QStringLiteral("tts")));
			QCOMPARE(events.size(), 2);
			QVERIFY(events.at(0).stop);
			QCOMPARE(events.at(1).text, QStringLiteral("speech on"));
			QVERIFY(events.at(1).interrupt);

			QMudNativePluginRegistry::handleScreenDraw(&runtime, 1, 0, QStringLiteral("spoken"));
			QCOMPARE(events.size(), 3);
			QCOMPARE(events.at(2).text, QStringLiteral("spoken"));

			QMudNativePluginRegistry::handleTabComplete(&runtime, QStringLiteral("north"));
			QCOMPARE(events.size(), 4);
			QCOMPARE(events.at(3).text, QStringLiteral("north"));

			QVERIFY(QMudNativePluginRegistry::handleCommand(&runtime, QStringLiteral("tts")));
			QCOMPARE(events.size(), 6);
			QVERIFY(events.at(4).stop);
			QCOMPARE(events.at(5).text, QStringLiteral("speech off"));
			QVERIFY(events.at(5).interrupt);

			QMudNativePluginRegistry::handleScreenDraw(&runtime, 1, 0, QStringLiteral("muted"));
			QCOMPARE(events.size(), 6);
		}

		void runtimeSetupRegistersNativeAccelerator()
		{
			WorldRuntime runtime;
			QMudNativePluginRegistry::ensureRuntimeSetup(&runtime);

			bool found = false;
			for (auto it = stateFor(&runtime).acceleratorKeyToCommand.constBegin();
			     it != stateFor(&runtime).acceleratorKeyToCommand.constEnd(); ++it)
			{
				const int commandId = it.value();
				if (runtime.acceleratorCommandText(commandId) == QStringLiteral("tts"))
				{
					found = true;
					QCOMPARE(runtime.acceleratorSendTarget(commandId), eSendToExecute);
					QCOMPARE(runtime.acceleratorPluginId(commandId),
					         QMudNativePluginRegistry::mushReaderPluginId());
				}
			}
			QVERIFY(found);
		}

		void passiveSpeechEnabledStateTracksTtsToggle()
		{
			WorldRuntime runtime;
			QMudNativePluginRegistry::setTestSpeechSink(
			    [](const QMudNativePluginRegistry::TestSpeechEvent &) {});
			QVERIFY(!QMudNativePluginRegistry::isPassiveSpeechEnabled(&runtime));

			QVERIFY(QMudNativePluginRegistry::handleCommand(&runtime, QStringLiteral("tts")));
			QVERIFY(QMudNativePluginRegistry::isPassiveSpeechEnabled(&runtime));

			QVERIFY(QMudNativePluginRegistry::handleCommand(&runtime, QStringLiteral("tts")));
			QVERIFY(!QMudNativePluginRegistry::isPassiveSpeechEnabled(&runtime));
		}

		void blacklistAndProtectedPluginXmlClassification()
		{
			const QStringList blacklistIds{QStringLiteral("bb6a05ed7534b5db1ed40511"),
			                               QStringLiteral("b8e6dac1ee7fe8e3de931fb7"),
			                               QStringLiteral("8238deec7c06bade8ebc3819")};
			for (const QString &id : blacklistIds)
			{
				QVERIFY(QMudNativePluginRegistry::isBlacklistedId(id));
				QVERIFY(QMudNativePluginRegistry::isProtectedId(id));
				QVERIFY(!QMudNativePluginRegistry::isShimId(id));
				QVERIFY(!QMudNativePluginRegistry::pluginInfo(id, 1).isValid());
				QCOMPARE(QMudNativePluginRegistry::pluginSupports(id, QStringLiteral("say")), eNoSuchPlugin);
			}

			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			for (const QString &id :
			     blacklistIds + QStringList{QMudNativePluginRegistry::mushReaderPluginId()})
			{
				const QString path = writePluginXml(dir, id, QStringLiteral("package.loadlib('x','y')"));
				QVERIFY(!path.isEmpty());
				WorldDocument doc;
				QVERIFY(doc.loadFromPluginFile(path));
				QVERIFY(!doc.plugins().isEmpty());
				const QString parsedId =
				    doc.plugins().front().attributes.value(QStringLiteral("id")).trimmed().toLower();
				QCOMPARE(parsedId, id);
				QVERIFY(QMudNativePluginRegistry::isProtectedId(parsedId));
			}
		}
		// NOLINTEND(readability-convert-member-functions-to-static)
};

QTEST_MAIN(tst_NativePluginRegistry)

#if __has_include("tst_NativePluginRegistry.moc")
#include "tst_NativePluginRegistry.moc"
#endif
