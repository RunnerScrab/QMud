/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: NoteColourUtils.h
 * Role: Helpers for MUSHclient-compatible note colour option encoding.
 */

#ifndef QMUD_NOTE_COLOUR_UTILS_H
#define QMUD_NOTE_COLOUR_UTILS_H

#include <QColor>
#include <QString>
#include <QVector>

namespace QMudNoteColour
{
	/**
	 * @brief Default MUSHclient public note color index.
	 */
	constexpr int         kDefaultPublicIndex = 5;

	/**
	 * @brief Converts runtime note color index to public SetNoteColour/GetNoteColour index.
	 * @param runtimeIndex Runtime index, where `-1` means same/default color.
	 * @return Public index in range `0..16`.
	 */
	[[nodiscard]] int     publicIndexFromRuntimeIndex(int runtimeIndex);

	/**
	 * @brief Converts a persisted `note_text_color` value to public note color index.
	 * @param value Persisted world attribute value.
	 * @param fallbackPublicIndex Public index used when value is empty or invalid.
	 * @return Public index in range `0..16`.
	 */
	[[nodiscard]] int     publicIndexFromWorldAttribute(const QString &value,
	                                                    int            fallbackPublicIndex = kDefaultPublicIndex);

	/**
	 * @brief Converts a persisted `note_text_color` value using custom text colors for legacy RGB values.
	 * @param value Persisted world attribute value.
	 * @param customTextColours Custom text colors indexed as `Custom1-Custom16`.
	 * @param fallbackPublicIndex Public index used when value is empty or invalid.
	 * @return Public index in range `0..16`.
	 */
	[[nodiscard]] int     publicIndexFromWorldAttribute(const QString         &value,
	                                                    const QVector<QColor> &customTextColours,
	                                                    int fallbackPublicIndex = kDefaultPublicIndex);

	/**
	 * @brief Encodes public note color index as MUSHclient-compatible world attribute value.
	 * @param publicIndex Public index in range `0..16`.
	 * @return Persisted `note_text_colour` value.
	 */
	[[nodiscard]] QString worldAttributeFromPublicIndex(int publicIndex);
} // namespace QMudNoteColour

#endif // QMUD_NOTE_COLOUR_UTILS_H
