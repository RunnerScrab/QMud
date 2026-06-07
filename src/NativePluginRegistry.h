/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: NativePluginRegistry.h
 * Role: Native compatibility plugin registry and shim dispatch API.
 */

#ifndef QMUD_NATIVEPLUGINREGISTRY_H
#define QMUD_NATIVEPLUGINREGISTRY_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#ifdef QMUD_NATIVEPLUGINREGISTRY_TEST_HOOKS
#include <functional>
#endif

class WorldRuntime;

namespace QMudNativePluginRegistry
{
	/**
	 * @brief Metadata exposed by a native compatibility shim through legacy plugin APIs.
	 */
	struct NativePluginMetadata
	{
			QString   id;
			QString   name;
			QString   author;
			QString   purpose;
			QString   description;
			QString   language;
			QString   source;
			QString   directory;
			double    version{0.0};
			double    requiredVersion{0.0};
			int       sequence{5000};
			QDateTime dateWritten;
			QDateTime dateModified;
	};

	/**
	 * @brief Return payload for native CallPlugin dispatch.
	 */
	struct NativeCallResult
	{
			int               errorCode{0};
			QString           errorText;
			QVector<QVariant> returnValues;
	};

	/**
	 * @brief Returns the exact UI marker used for native shim shadow rows.
	 * @return Native compatibility marker text.
	 */
	[[nodiscard]] QString     nativeShimMarkerText();
	/**
	 * @brief Returns the MushReader legacy plugin id implemented natively.
	 * @return Lower-case plugin id.
	 */
	[[nodiscard]] QString     mushReaderPluginId();
	/**
	 * @brief Checks whether a plugin id is implemented as a native shim.
	 * @param pluginId Plugin id to check.
	 * @return `true` when the id maps to a native shim.
	 */
	[[nodiscard]] bool        isShimId(const QString &pluginId);
	/**
	 * @brief Checks whether a plugin id is centrally denied without a shim.
	 * @param pluginId Plugin id to check.
	 * @return `true` when loading should be ignored.
	 */
	[[nodiscard]] bool        isBlacklistedId(const QString &pluginId);
	/**
	 * @brief Checks whether a plugin id is protected by either shim or blacklist policy.
	 * @param pluginId Plugin id to check.
	 * @return `true` when the normal XML plugin loader must not execute it.
	 */
	[[nodiscard]] bool        isProtectedId(const QString &pluginId);
	/**
	 * @brief Resolves a plugin id or native shim name to a canonical shim id.
	 * @param pluginIdOrName Plugin id or display name.
	 * @return Canonical shim id, or empty when not a shim.
	 */
	[[nodiscard]] QString     resolveShimIdOrName(const QString &pluginIdOrName);
	/**
	 * @brief Returns native shim metadata.
	 * @param pluginId Shim id.
	 * @param metadata Output metadata.
	 * @return `true` when metadata exists.
	 */
	[[nodiscard]] bool        metadataForShim(const QString &pluginId, NativePluginMetadata &metadata);
	/**
	 * @brief Lists native routines exposed by a shim.
	 * @param pluginId Shim id.
	 * @return Routine names in stable order.
	 */
	[[nodiscard]] QStringList supportedRoutines(const QString &pluginId);
	/**
	 * @brief Returns a legacy PluginSupports code for a native shim routine.
	 * @param pluginId Shim id.
	 * @param routine Routine name.
	 * @return Legacy scripting error/status code.
	 */
	[[nodiscard]] int         pluginSupports(const QString &pluginId, const QString &routine);
	/**
	 * @brief Returns a legacy GetPluginInfo value for a native shim.
	 * @param pluginId Shim id.
	 * @param infoType Legacy info selector.
	 * @param visibleIndex One-based list index when a shadow row exists, otherwise zero.
	 * @param enabled Runtime-visible enabled state for info selector 17.
	 * @return Requested value, or invalid QVariant when unsupported.
	 */
	[[nodiscard]] QVariant    pluginInfo(const QString &pluginId, int infoType, int visibleIndex = 0,
	                                     bool enabled = false);
	/**
	 * @brief Calls a native shim routine from legacy CallPlugin APIs.
	 * @param runtime Owning runtime.
	 * @param pluginId Shim id.
	 * @param routine Routine name.
	 * @param arguments Routine arguments converted to variants.
	 * @return Legacy status plus optional return values.
	 */
	NativeCallResult callRoutine(const WorldRuntime *runtime, const QString &pluginId, const QString &routine,
	                             const QVector<QVariant> &arguments);
	/**
	 * @brief Handles native command aliases that would normally be installed by MushReader XML.
	 * @param runtime Owning runtime.
	 * @param command Entered command line.
	 * @return `true` when the command was consumed.
	 */
	bool             handleCommand(WorldRuntime *runtime, const QString &command);
	/**
	 * @brief Handles native screen-draw speech for MushReader parity.
	 * @param runtime Owning runtime.
	 * @param type Legacy screen-draw type.
	 * @param log Legacy log flag.
	 * @param text Text drawn to the output surface.
	 */
	void             handleScreenDraw(const WorldRuntime *runtime, int type, int log, const QString &text);
	/**
	 * @brief Handles native tab-completion speech for MushReader parity.
	 * @param runtime Owning runtime.
	 * @param text Completed command text.
	 */
	void             handleTabComplete(const WorldRuntime *runtime, const QString &text);
	/**
	 * @brief Enables or disables passive native MushReader screen/tab speech.
	 * @param runtime Owning runtime.
	 * @param enable Enable passive screen/tab speech when `true`.
	 */
	void             setPassiveSpeechEnabled(const WorldRuntime *runtime, bool enable);
	/**
	 * @brief Returns whether passive native MushReader screen/tab speech is active.
	 * @param runtime Owning runtime.
	 * @return `true` when passive speech is enabled for the runtime.
	 */
	[[nodiscard]] bool isPassiveSpeechEnabled(const WorldRuntime *runtime);
	/**
	 * @brief Installs runtime-level native behavior that must exist even without a shadow row.
	 * @param runtime Owning runtime.
	 */
	void               ensureRuntimeSetup(WorldRuntime *runtime);
	/**
	 * @brief Releases per-runtime native shim state.
	 * @param runtime Runtime being destroyed.
	 */
	void               discardRuntimeState(const WorldRuntime *runtime);

#ifdef QMUD_NATIVEPLUGINREGISTRY_TEST_HOOKS
	/**
	 * @brief Speech event captured by native registry test hooks.
	 */
	struct TestSpeechEvent
	{
			QString text;
			bool    interrupt{false};
			bool    stop{false};
	};

	/**
	 * @brief Installs a test-only speech sink that bypasses platform reader backends.
	 * @param sink Event receiver; empty sink restores normal backend routing.
	 */
	void setTestSpeechSink(std::function<void(const TestSpeechEvent &)> sink);
#endif
} // namespace QMudNativePluginRegistry

#endif // QMUD_NATIVEPLUGINREGISTRY_H
