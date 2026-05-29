/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: GuiSystemUtils.h
 * Role: Shared GUI/system snapshot helpers for Lua-compatible API values.
 */

#ifndef QMUD_GUISYSTEMUTILS_H
#define QMUD_GUISYSTEMUTILS_H

// ReSharper disable once CppUnusedIncludeDirective
#include <QString>
#include <QVariantMap>

/**
 * @brief Returns canonical key used for GUI/system snapshot values.
 * @param category Value category name.
 * @param index Category-specific numeric index.
 * @return Snapshot key.
 */
QString     qmudGuiSystemValueKey(const QString &category, int index);
/**
 * @brief Returns Windows-compatible system color value for the current GUI palette.
 * @param index Legacy GetSysColor index.
 * @return Packed RGB color value, or zero for unsupported indexes.
 */
long        qmudGuiSystemColor(int index);
/**
 * @brief Collects GUI/system values used by Lua GetDeviceCaps/GetSystemMetrics/GetSysColor APIs.
 * @return Snapshot map keyed by qmudGuiSystemValueKey plus named values.
 */
QVariantMap qmudCollectGuiSystemValues();

#endif // QMUD_GUISYSTEMUTILS_H
