/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: NativePluginRegistry.cpp
 * Role: Native compatibility plugin registry and MushReader shim implementation.
 */

#include "NativePluginRegistry.h"

#include "AcceleratorUtils.h"
#include "TtsEngine.h"
#include "WorldOptions.h"
#include "WorldRuntime.h"
#include "scripting/ScriptingErrors.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QMutex>
// ReSharper disable once CppUnusedIncludeDirective
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <objbase.h>
#include <oleauto.h>
#include <windows.h>
#endif

namespace
{
	constexpr auto kMushReaderPluginId = "925cdd0331023d9f0b8f05a7"; // MushReader.xml
	constexpr auto kNativeShimMarker   = "QMud native compatibility shim - Legacy XML ignored.";

	QString        normalizedId(const QString &value)
	{
		return value.trimmed().toLower();
	}

	QString normalizedRoutine(const QString &value)
	{
		return value.trimmed().toLower();
	}

	const QSet<QString> &shimIds()
	{
		static const QSet<QString> ids{QString::fromLatin1(kMushReaderPluginId)};
		return ids;
	}

	const QSet<QString> &blacklistedIds()
	{
		static const QSet<QString> ids{
		    QStringLiteral("bb6a05ed7534b5db1ed40511"), // Automatic_Backup.xml
		    QStringLiteral("b8e6dac1ee7fe8e3de931fb7"), // Automatic_Backup_7Days.xml
		    QStringLiteral("8238deec7c06bade8ebc3819")  // AutoSave.xml
		};
		return ids;
	}

	QString qmudNativeSourceName(const QString &name)
	{
		return QStringLiteral("qmud:native/%1").arg(name);
	}

	QMudNativePluginRegistry::NativePluginMetadata mushReaderMetadata()
	{
		QMudNativePluginRegistry::NativePluginMetadata metadata;
		metadata.id          = QString::fromLatin1(kMushReaderPluginId);
		metadata.name        = QStringLiteral("MushReader");
		metadata.author      = QStringLiteral("QMud native compatibility layer");
		metadata.purpose     = QStringLiteral("Native screen-reader compatibility shim");
		metadata.description = QStringLiteral("MushReader is implemented natively by QMud. Legacy XML and "
		                                      "DLL content with this plugin id is "
		                                      "intentionally shadowed and never executed. %1")
		                           .arg(QString::fromLatin1(kNativeShimMarker));
		metadata.language    = QStringLiteral("native");
		metadata.source      = qmudNativeSourceName(metadata.name);
		metadata.directory   = QStringLiteral("qmud:native/");
		metadata.version     = 1.10;
		metadata.requiredVersion = 0.0;
		metadata.sequence        = 5000;
		metadata.dateWritten     = QDateTime::fromString(QStringLiteral("2026-06-06T00:00:00Z"), Qt::ISODate);
		metadata.dateModified    = metadata.dateWritten;
		return metadata;
	}

	const QStringList &mushReaderRoutines()
	{
		static const QStringList routines{QStringLiteral("say"), QStringLiteral("interrupt"),
		                                  QStringLiteral("stop"), QStringLiteral("plugin_update_url")};
		return routines;
	}

	QString luaQuote(const QString &value)
	{
		QString quoted;
		quoted.reserve(value.size() + 8);
		for (const QChar ch : value)
		{
			switch (ch.unicode())
			{
			case '\\':
				quoted += QStringLiteral("\\\\");
				break;
			case '"':
				quoted += QStringLiteral("\\\"");
				break;
			case '\n':
				quoted += QStringLiteral("\\n");
				break;
			case '\r':
				quoted += QStringLiteral("\\r");
				break;
			case '\t':
				quoted += QStringLiteral("\\t");
				break;
			default:
				quoted += ch;
				break;
			}
		}
		return quoted;
	}

	QString luaUnquote(const QString &value)
	{
		QString unquoted;
		unquoted.reserve(value.size());
		bool escape = false;
		for (const QChar ch : value)
		{
			if (!escape)
			{
				if (ch == QLatin1Char('\\'))
				{
					escape = true;
					continue;
				}
				unquoted += ch;
				continue;
			}
			escape = false;
			switch (ch.unicode())
			{
			case 'n':
				unquoted += QLatin1Char('\n');
				break;
			case 'r':
				unquoted += QLatin1Char('\r');
				break;
			case 't':
				unquoted += QLatin1Char('\t');
				break;
			default:
				unquoted += ch;
				break;
			}
		}
		if (escape)
			unquoted += QLatin1Char('\\');
		return unquoted;
	}

	class ReaderBackend
	{
		public:
			virtual ~ReaderBackend()                                = default;
			virtual bool speak(const QString &text, bool interrupt) = 0;
			virtual bool stop()                                     = 0;
	};

#ifdef Q_OS_WIN
	class NvdaReaderBackend final : public ReaderBackend
	{
		public:
			NvdaReaderBackend()
			{
				const QString appDir    = QCoreApplication::applicationDirPath();
				const QString nativeDir = QDir(appDir).filePath(QStringLiteral("lua/native/windows-x86_64"));
				for (const QString &name : {QStringLiteral("nvdaControllerClient64.dll"),
				                            QStringLiteral("nvdaControllerClient.dll")})
				{
					for (const QString &candidate :
					     {QDir(appDir).filePath(name), QDir(nativeDir).filePath(name), name})
					{
						m_library.setFileName(candidate);
						if (!m_library.load())
							continue;
						m_testIfRunning = reinterpret_cast<TestIfRunning>(
						    m_library.resolve("nvdaController_testIfRunning"));
						m_speakText =
						    reinterpret_cast<SpeakText>(m_library.resolve("nvdaController_speakText"));
						m_cancelSpeech =
						    reinterpret_cast<CancelSpeech>(m_library.resolve("nvdaController_cancelSpeech"));
						if (m_testIfRunning && m_speakText && m_cancelSpeech)
							return;
						m_library.unload();
					}
				}
			}

			bool speak(const QString &text, const bool interrupt) override
			{
				if (!available())
					return false;
				if (interrupt)
					static_cast<void>(m_cancelSpeech());
				return m_speakText(reinterpret_cast<const wchar_t *>(text.utf16())) == 0;
			}

			bool stop() override
			{
				return available() && m_cancelSpeech() == 0;
			}

		private:
			using TestIfRunning = long(__stdcall *)();
			using SpeakText     = long(__stdcall *)(const wchar_t *);
			using CancelSpeech  = long(__stdcall *)();

			bool available() const
			{
				return m_testIfRunning && m_speakText && m_cancelSpeech && m_testIfRunning() == 0;
			}

			QLibrary      m_library;
			TestIfRunning m_testIfRunning{nullptr};
			SpeakText     m_speakText{nullptr};
			CancelSpeech  m_cancelSpeech{nullptr};
	};

	class JawsReaderBackend final : public ReaderBackend
	{
		public:
			JawsReaderBackend()
			{
				const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
				m_uninitializeCom        = SUCCEEDED(initResult);
				if (!SUCCEEDED(initResult) && initResult != RPC_E_CHANGED_MODE)
					return;

				IDispatch *dispatch = nullptr;
				if (!createDispatch(L"freedomsci.jawsapi", &dispatch) &&
				    !createDispatch(L"jfwapi", &dispatch))
					return;
				m_dispatch = dispatch;

				OLECHAR *name = const_cast<OLECHAR *>(L"SayString");
				if (FAILED(
				        m_dispatch->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &m_sayStringId)))
				{
					m_dispatch->Release();
					m_dispatch = nullptr;
				}
			}

			~JawsReaderBackend() override
			{
				if (m_dispatch)
					m_dispatch->Release();
				if (m_uninitializeCom)
					CoUninitialize();
			}

			bool speak(const QString &text, const bool interrupt) override
			{
				return sayString(text, interrupt ? 1 : 0);
			}

			bool stop() override
			{
				return sayString(QString(), 1);
			}

		private:
			static bool createDispatch(const wchar_t *progId, IDispatch **dispatch)
			{
				CLSID clsid;
				if (FAILED(CLSIDFromProgID(progId, &clsid)))
					return false;
				return SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER | CLSCTX_INPROC_SERVER,
				                                  IID_IDispatch, reinterpret_cast<void **>(dispatch)));
			}

			bool sayString(const QString &text, const int interrupt) const
			{
				if (!m_dispatch || m_sayStringId == DISPID_UNKNOWN)
					return false;

				VARIANTARG args[2];
				VariantInit(&args[0]);
				VariantInit(&args[1]);
				args[0].vt      = VT_I4;
				args[0].lVal    = interrupt;
				args[1].vt      = VT_BSTR;
				args[1].bstrVal = SysAllocString(reinterpret_cast<const OLECHAR *>(text.utf16()));
				if (!args[1].bstrVal)
					return false;

				DISPPARAMS params{};
				params.cArgs  = 2;
				params.rgvarg = args;
				VARIANT result;
				VariantInit(&result);
				const HRESULT hr = m_dispatch->Invoke(m_sayStringId, IID_NULL, LOCALE_USER_DEFAULT,
				                                      DISPATCH_METHOD, &params, &result, nullptr, nullptr);
				VariantClear(&result);
				VariantClear(&args[1]);
				return SUCCEEDED(hr);
			}

			IDispatch *m_dispatch{nullptr};
			DISPID     m_sayStringId{DISPID_UNKNOWN};
			bool       m_uninitializeCom{false};
	};
#endif

	class TtsReaderBackend final : public ReaderBackend
	{
		public:
			bool speak(const QString &text, const bool interrupt) override
			{
				if (!m_tts)
					m_tts = QMudTtsEngine::TtsEngine::create();
				if (!m_tts)
					return false;
				m_tts->enqueueUtterance(text, interrupt);
				return true;
			}

			bool stop() override
			{
				if (!m_tts)
					m_tts = QMudTtsEngine::TtsEngine::create();
				if (!m_tts)
					return false;
				m_tts->enqueueUtterance(QString(), true);
				return true;
			}

		private:
			std::shared_ptr<QMudTtsEngine::TtsEngine> m_tts;
	};

	struct MushReaderState
	{
			bool                                        passiveSpeechEnabled{false};
			bool                                        substitutionsEnabled{true};
			bool                                        substitutionsLoaded{false};
			bool                                        runtimeSetupComplete{false};
			QMap<QString, QString>                      substitutions;
			std::vector<std::unique_ptr<ReaderBackend>> backends;
	};

	QMutex &stateMutex()
	{
		static QMutex mutex;
		return mutex;
	}

	std::unordered_map<const WorldRuntime *, std::unique_ptr<MushReaderState>> &states()
	{
		static std::unordered_map<const WorldRuntime *, std::unique_ptr<MushReaderState>> runtimeStates;
		return runtimeStates;
	}

#ifdef QMUD_NATIVEPLUGINREGISTRY_TEST_HOOKS
	QMudNativePluginRegistry::TestSpeechEvent makeTestSpeechEvent(const QString &text, const bool interrupt,
	                                                              const bool stop)
	{
		QMudNativePluginRegistry::TestSpeechEvent event;
		event.text      = text;
		event.interrupt = interrupt;
		event.stop      = stop;
		return event;
	}

	std::function<void(const QMudNativePluginRegistry::TestSpeechEvent &)> &testSpeechSink()
	{
		static std::function<void(const QMudNativePluginRegistry::TestSpeechEvent &)> sink;
		return sink;
	}

	bool dispatchTestSpeechEvent(const QMudNativePluginRegistry::TestSpeechEvent &event)
	{
		QMutexLocker locker(&stateMutex());
		auto        &sink = testSpeechSink();
		if (!sink)
			return false;
		sink(event);
		return true;
	}
#endif

	MushReaderState &stateFor(const WorldRuntime *runtime)
	{
		QMutexLocker locker(&stateMutex());
		auto        &slot = states()[runtime];
		if (!slot)
			slot = std::make_unique<MushReaderState>();
		return *slot;
	}

	QString substitutionsFilePath(const WorldRuntime *runtime)
	{
		const QString base = runtime->startupDirectory().trimmed();
		return QDir(base.isEmpty() ? QDir::currentPath() : base)
		    .filePath(QStringLiteral("substitutions.mush"));
	}

	void ensureBackends(MushReaderState &state)
	{
		if (!state.backends.empty())
			return;
#ifdef Q_OS_WIN
		state.backends.push_back(std::make_unique<NvdaReaderBackend>());
		state.backends.push_back(std::make_unique<JawsReaderBackend>());
#endif
		state.backends.push_back(std::make_unique<TtsReaderBackend>());
	}

	void loadSubstitutions(const WorldRuntime *runtime, MushReaderState &state)
	{
		if (state.substitutionsLoaded)
			return;
		state.substitutionsLoaded = true;
		const QString path        = substitutionsFilePath(runtime);
		QFile         file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return;
		const QString            text = QString::fromUtf8(file.readAll());
		const QRegularExpression statusRegex(QStringLiteral(R"(\bstatus\s*=\s*([01]))"));
		if (const QRegularExpressionMatch match = statusRegex.match(text); match.hasMatch())
			state.substitutionsEnabled = match.captured(1) != QStringLiteral("0");

		const QRegularExpression entryRegex(
		    QStringLiteral(R"rx(\[\s*"((?:\\.|[^"\\])*)"\s*\]\s*=\s*"((?:\\.|[^"\\])*)")rx"));
		auto it = entryRegex.globalMatch(text);
		while (it.hasNext())
		{
			const QRegularExpressionMatch match = it.next();
			state.substitutions.insert(luaUnquote(match.captured(1)), luaUnquote(match.captured(2)));
		}
	}

	bool saveSubstitutions(const WorldRuntime *runtime, const MushReaderState &state)
	{
		const QString path = substitutionsFilePath(runtime);
		QSaveFile     file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
			return false;
		QTextStream out(&file);
		out << "return {\n";
		out << "  status = " << (state.substitutionsEnabled ? 1 : 0) << ",\n";
		for (auto it = state.substitutions.constBegin(); it != state.substitutions.constEnd(); ++it)
			out << "  [\"" << luaQuote(it.key()) << "\"] = \"" << luaQuote(it.value()) << "\",\n";
		out << "}\n";
		return file.commit();
	}

	bool speak(const WorldRuntime *runtime, const QString &text, const bool interrupt)
	{
		if (!runtime)
			return false;
#ifdef QMUD_NATIVEPLUGINREGISTRY_TEST_HOOKS
		if (dispatchTestSpeechEvent(makeTestSpeechEvent(text, interrupt, false)))
			return true;
#endif
		MushReaderState &state = stateFor(runtime);
		ensureBackends(state);
		for (const auto &backend : state.backends)
		{
			if (backend && backend->speak(text, interrupt))
				return true;
		}
		return false;
	}

	bool stopSpeech(const WorldRuntime *runtime)
	{
		if (!runtime)
			return false;
#ifdef QMUD_NATIVEPLUGINREGISTRY_TEST_HOOKS
		if (dispatchTestSpeechEvent(makeTestSpeechEvent(QString(), true, true)))
			return true;
#endif
		MushReaderState &state = stateFor(runtime);
		ensureBackends(state);
		bool stopped = false;
		for (const auto &backend : state.backends)
		{
			if (backend)
				stopped = backend->stop() || stopped;
		}
		return stopped;
	}

	void installMushReaderAccelerator(WorldRuntime *runtime, MushReaderState &state)
	{
		if (state.runtimeSetupComplete)
			return;
		state.runtimeSetupComplete = true;

		quint32 virt = 0;
		quint16 key  = 0;
		if (!AcceleratorUtils::stringToAccelerator(QStringLiteral("Ctrl+Shift+F12"), virt, key))
			return;

		const qint64 mapKey = (static_cast<qint64>(virt) << 16) | key;
		if (runtime->acceleratorCommandForKey(mapKey) >= 0)
			return;

		const int commandId = runtime->allocateAcceleratorCommand();
		if (commandId < 0)
			return;

		WorldRuntime::AcceleratorEntry entry;
		entry.text     = QStringLiteral("tts");
		entry.sendTo   = eSendToExecute;
		entry.pluginId = QString::fromLatin1(kMushReaderPluginId);
		runtime->registerAccelerator(mapKey, commandId, entry);
	}

	void outputLine(WorldRuntime &runtime, const QString &line)
	{
		runtime.outputText(line, true, true);
	}

	void handleSubstitutionCommand(WorldRuntime *runtime, const QString &argument)
	{
		MushReaderState &state = stateFor(runtime);
		loadSubstitutions(runtime, state);

		const QString trimmed = argument.trimmed();
		const QString lower   = trimmed.toLower();
		if (trimmed.isEmpty() || lower == QStringLiteral("help"))
		{
			outputLine(*runtime, QStringLiteral(
			                         "subst on|off|list|clear|add <text>==<replacement>|remove <text>|save"));
			return;
		}
		if (lower == QStringLiteral("on"))
		{
			state.substitutionsEnabled = true;
			saveSubstitutions(runtime, state);
			outputLine(*runtime, QStringLiteral("Substitutions on."));
			return;
		}
		if (lower == QStringLiteral("off"))
		{
			state.substitutionsEnabled = false;
			saveSubstitutions(runtime, state);
			outputLine(*runtime, QStringLiteral("Substitutions off."));
			return;
		}
		if (lower == QStringLiteral("list"))
		{
			if (state.substitutions.isEmpty())
			{
				outputLine(*runtime, QStringLiteral("No substitutions."));
				return;
			}
			for (auto it = state.substitutions.constBegin(); it != state.substitutions.constEnd(); ++it)
				outputLine(*runtime, QStringLiteral("%1 == %2").arg(it.key(), it.value()));
			return;
		}
		if (lower == QStringLiteral("clear"))
		{
			state.substitutions.clear();
			saveSubstitutions(runtime, state);
			outputLine(*runtime, QStringLiteral("Substitutions cleared."));
			return;
		}
		if (lower == QStringLiteral("save"))
		{
			outputLine(*runtime, saveSubstitutions(runtime, state)
			                         ? QStringLiteral("Substitutions saved.")
			                         : QStringLiteral("Unable to save substitutions."));
			return;
		}
		if (lower.startsWith(QStringLiteral("remove ")))
		{
			const QString key = trimmed.mid(7).trimmed();
			state.substitutions.remove(key);
			saveSubstitutions(runtime, state);
			outputLine(*runtime, QStringLiteral("Substitution removed."));
			return;
		}
		if (lower.startsWith(QStringLiteral("add ")))
		{
			const QString payload = trimmed.mid(4);
			const int     sep     = payload.indexOf(QStringLiteral("=="));
			if (sep < 0)
			{
				outputLine(*runtime, QStringLiteral("Substitution add requires <text>==<replacement>."));
				return;
			}
			const QString key   = payload.left(sep).trimmed();
			const QString value = payload.mid(sep + 2).trimmed();
			if (key.isEmpty())
			{
				outputLine(*runtime, QStringLiteral("Substitution text cannot be empty."));
				return;
			}
			state.substitutions.insert(key, value);
			saveSubstitutions(runtime, state);
			outputLine(*runtime, QStringLiteral("Substitution added."));
			return;
		}
		outputLine(*runtime, QStringLiteral("Unknown substitution command."));
	}

	QString substitutionAppliedText(const WorldRuntime *runtime, const QString &text, bool &skip)
	{
		skip                   = false;
		MushReaderState &state = stateFor(runtime);
		loadSubstitutions(runtime, state);
		if (!state.substitutionsEnabled)
		{
			skip = true;
			return {};
		}
		const auto it = state.substitutions.constFind(text);
		if (it == state.substitutions.constEnd())
			return text;
		if (it.value() == QStringLiteral("!skip"))
		{
			skip = true;
			return {};
		}
		return it.value();
	}

	QString firstArgumentString(const QVector<QVariant> &arguments)
	{
		return arguments.isEmpty() ? QString() : arguments.constFirst().toString();
	}
} // namespace

namespace QMudNativePluginRegistry
{
	QString nativeShimMarkerText()
	{
		return QString::fromLatin1(kNativeShimMarker);
	}

	QString mushReaderPluginId()
	{
		return QString::fromLatin1(kMushReaderPluginId);
	}

	bool isShimId(const QString &pluginId)
	{
		return shimIds().contains(normalizedId(pluginId));
	}

	bool isBlacklistedId(const QString &pluginId)
	{
		return blacklistedIds().contains(normalizedId(pluginId));
	}

	bool isProtectedId(const QString &pluginId)
	{
		return isShimId(pluginId) || isBlacklistedId(pluginId);
	}

	QString resolveShimIdOrName(const QString &pluginIdOrName)
	{
		const QString key = normalizedId(pluginIdOrName);
		if (key.isEmpty())
			return {};
		if (isShimId(key))
			return key;
		NativePluginMetadata metadata;
		if (metadataForShim(QString::fromLatin1(kMushReaderPluginId), metadata) &&
		    metadata.name.compare(pluginIdOrName.trimmed(), Qt::CaseInsensitive) == 0)
		{
			return metadata.id;
		}
		return {};
	}

	bool metadataForShim(const QString &pluginId, NativePluginMetadata &metadata)
	{
		if (!isShimId(pluginId))
			return false;
		if (normalizedId(pluginId) == QString::fromLatin1(kMushReaderPluginId))
		{
			metadata = mushReaderMetadata();
			return true;
		}
		return false;
	}

	QStringList supportedRoutines(const QString &pluginId)
	{
		if (normalizedId(pluginId) == QString::fromLatin1(kMushReaderPluginId))
			return mushReaderRoutines();
		return {};
	}

	int pluginSupports(const QString &pluginId, const QString &routine)
	{
		const QString normalized = normalizedRoutine(routine);
		if (!isShimId(pluginId))
			return eNoSuchPlugin;
		if (normalized.isEmpty())
			return eNoSuchRoutine;
		for (const QString &candidate : supportedRoutines(pluginId))
		{
			if (candidate.compare(normalized, Qt::CaseInsensitive) == 0)
				return eOK;
		}
		return eNoSuchRoutine;
	}

	QVariant pluginInfo(const QString &pluginId, const int infoType, const int visibleIndex)
	{
		NativePluginMetadata metadata;
		if (!metadataForShim(pluginId, metadata))
			return {};
		switch (infoType)
		{
		case 1:
			return metadata.name;
		case 2:
			return metadata.author;
		case 3:
			return metadata.description;
		case 4:
			return QString();
		case 5:
			return metadata.language;
		case 6:
			return metadata.source;
		case 7:
			return metadata.id;
		case 8:
			return metadata.purpose;
		case 9:
		case 10:
		case 11:
		case 12:
			return 0;
		case 13:
			return metadata.dateWritten;
		case 14:
			return metadata.dateModified;
		case 15:
		case 16:
			return false;
		case 17:
			return true;
		case 18:
			return metadata.requiredVersion;
		case 19:
			return metadata.version;
		case 20:
			return metadata.directory;
		case 21:
			return visibleIndex;
		case 22:
			return metadata.dateModified;
		case 23:
			return QString();
		case 24:
			return 0.0;
		case 25:
			return metadata.sequence;
		default:
			return {};
		}
	}

	NativeCallResult callRoutine(const WorldRuntime *runtime, const QString &pluginId, const QString &routine,
	                             const QVector<QVariant> &arguments)
	{
		NativeCallResult result;
		result.errorCode = eOK;
		const QString id = normalizedId(pluginId);
		if (id != QString::fromLatin1(kMushReaderPluginId))
		{
			result.errorCode = eNoSuchPlugin;
			result.errorText = QStringLiteral("Plugin ID (%1) is not installed").arg(pluginId);
			return result;
		}

		const QString name = normalizedRoutine(routine);
		if (name == QStringLiteral("say"))
		{
			speak(runtime, QStringLiteral("       ") + firstArgumentString(arguments), false);
			return result;
		}
		if (name == QStringLiteral("interrupt"))
		{
			speak(runtime, firstArgumentString(arguments), true);
			return result;
		}
		if (name == QStringLiteral("stop"))
		{
			stopSpeech(runtime);
			return result;
		}
		if (name == QStringLiteral("plugin_update_url"))
		{
			result.returnValues.push_back(QStringLiteral("qmud:native/MushReader"));
			return result;
		}
		result.errorCode = eNoSuchRoutine;
		result.errorText = QStringLiteral("No function '%1' in native shim '%2' (%3)")
		                       .arg(routine, QStringLiteral("MushReader"), id);
		return result;
	}

	bool handleCommand(WorldRuntime *runtime, const QString &command)
	{
		if (!runtime)
			return false;
		const QString trimmed = command.trimmed();
		const QString lower   = trimmed.toLower();
		if (lower == QStringLiteral("tts_stop"))
		{
			stopSpeech(runtime);
			return true;
		}
		if (lower == QStringLiteral("tts_interrupt") || lower.startsWith(QStringLiteral("tts_interrupt ")))
		{
			QString text = trimmed.mid(QStringLiteral("tts_interrupt").size()).trimmed();
			speak(runtime, text, true);
			return true;
		}
		if (lower == QStringLiteral("tts_note") || lower.startsWith(QStringLiteral("tts_note ")))
		{
			QString text = trimmed.mid(QStringLiteral("tts_note").size()).trimmed();
			speak(runtime, text, false);
			return true;
		}
		if (lower == QStringLiteral("tts"))
		{
			MushReaderState &state     = stateFor(runtime);
			state.passiveSpeechEnabled = !state.passiveSpeechEnabled;
			stopSpeech(runtime);
			speak(runtime,
			      state.passiveSpeechEnabled ? QStringLiteral("speech on") : QStringLiteral("speech off"),
			      true);
			return true;
		}
		if (lower == QStringLiteral("mushreader help") || lower == QStringLiteral("mushreader:help"))
		{
			outputLine(*runtime, QStringLiteral("MushReader native QMud shim."));
			outputLine(*runtime,
			           QStringLiteral("Commands: tts, tts_note <text>, tts_interrupt <text>, tts_stop."));
			outputLine(
			    *runtime,
			    QStringLiteral(
			        "Substitutions: subst on|off|list|clear|add <text>==<replacement>|remove <text>|save."));
			return true;
		}
		if (lower == QStringLiteral("subst") || lower.startsWith(QStringLiteral("subst ")))
		{
			handleSubstitutionCommand(runtime, trimmed.mid(QStringLiteral("subst").size()));
			return true;
		}
		return false;
	}

	void handleScreenDraw(const WorldRuntime *runtime, const int type, const int /*log*/, const QString &text)
	{
		if (!runtime || (type != 0 && type != 1) || text.trimmed().isEmpty())
			return;
		MushReaderState &state = stateFor(runtime);
		if (!state.passiveSpeechEnabled)
			return;
		bool    skip = false;
		QString line = substitutionAppliedText(runtime, text, skip);
		if (!skip && !line.isEmpty())
			speak(runtime, line, false);
	}

	void handleTabComplete(const WorldRuntime *runtime, const QString &text)
	{
		if (!runtime || text.trimmed().isEmpty())
			return;
		MushReaderState &state = stateFor(runtime);
		if (state.passiveSpeechEnabled)
			speak(runtime, text, false);
	}

	void setPassiveSpeechEnabled(const WorldRuntime *runtime, const bool enable)
	{
		if (!runtime)
			return;
		MushReaderState &state     = stateFor(runtime);
		state.passiveSpeechEnabled = enable;
		if (!enable)
			stopSpeech(runtime);
	}

	void ensureRuntimeSetup(WorldRuntime *runtime)
	{
		if (!runtime)
			return;
		MushReaderState &state = stateFor(runtime);
		installMushReaderAccelerator(runtime, state);
	}

	void discardRuntimeState(const WorldRuntime *runtime)
	{
		QMutexLocker locker(&stateMutex());
		states().erase(runtime);
	}

#ifdef QMUD_NATIVEPLUGINREGISTRY_TEST_HOOKS
	void setTestSpeechSink(std::function<void(const TestSpeechEvent &)> sink)
	{
		QMutexLocker locker(&stateMutex());
		testSpeechSink() = std::move(sink);
	}
#endif
} // namespace QMudNativePluginRegistry
