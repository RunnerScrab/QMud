/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: NoteColourUtils.cpp
 * Role: MUSHclient-compatible note colour option encoding helpers.
 */

#include "helpers/NoteColourUtils.h"

#include "WorldOptions.h"

#include <QColor>

namespace
{
	constexpr long kSameColourPackedValue = 0x00FFFFFF;

	struct ParsedNoteColour
	{
			long   packed{0};
			QColor rgb;
			bool   valid{false};
	};

	/**
	 * @brief Packs RGB into MUSHclient COLORREF order.
	 * @param colour Qt RGB color.
	 * @return COLORREF-style packed value.
	 */
	[[nodiscard]] long colourRefFromQColor(const QColor &colour)
	{
		return (static_cast<long>(colour.blue()) << 16) | (static_cast<long>(colour.green()) << 8) |
		       static_cast<long>(colour.red());
	}

	/**
	 * @brief Converts COLORREF-style value to a Qt color.
	 * @param value COLORREF-style packed value.
	 * @return Qt RGB color.
	 */
	[[nodiscard]] QColor qColorFromColourRef(const long value)
	{
		return {static_cast<int>(value & 0xFF), static_cast<int>((value >> 8) & 0xFF),
		        static_cast<int>((value >> 16) & 0xFF)};
	}

	/**
	 * @brief Clamps public note color index to the supported MUSHclient range.
	 * @param publicIndex Public note color index.
	 * @return Clamped public note color index.
	 */
	[[nodiscard]] int clampPublicIndex(const int publicIndex)
	{
		return qBound(0, publicIndex, MAX_CUSTOM);
	}

	/**
	 * @brief Parses a persisted note-color value as either MUSH COLORREF or Qt RGB text.
	 * @param value Persisted world attribute value.
	 * @return Parsed color information.
	 */
	[[nodiscard]] ParsedNoteColour parseNoteColourValue(const QString &value)
	{
		const QString trimmed = value.trimmed();
		if (trimmed.isEmpty())
			return {};

		bool ok     = false;
		long packed = trimmed.toLong(&ok, 0);
		if (ok)
			return {packed, qColorFromColourRef(packed), true};

		const QColor colour(trimmed);
		if (!colour.isValid())
			return {};
		return {colourRefFromQColor(colour), colour, true};
	}

	/**
	 * @brief Matches a legacy literal RGB note color against the custom text color table.
	 * @param colour Literal RGB note color.
	 * @param customTextColours Custom text colors indexed as `Custom1-Custom16`.
	 * @return Public index, or `-1` when no match exists.
	 */
	[[nodiscard]] int publicIndexFromLegacyRgb(const QColor &colour, const QVector<QColor> &customTextColours)
	{
		if (!colour.isValid())
			return -1;
		const qsizetype count = qMin(customTextColours.size(), qsizetype{MAX_CUSTOM});
		for (qsizetype i = 0; i < count; ++i)
		{
			const QColor &customColour = customTextColours.at(i);
			if (customColour.isValid() && customColour.rgb() == colour.rgb())
				return static_cast<int>(i) + 1;
		}
		return -1;
	}
} // namespace

namespace QMudNoteColour
{
	int publicIndexFromRuntimeIndex(const int runtimeIndex)
	{
		return runtimeIndex < 0 ? 0 : clampPublicIndex(runtimeIndex + 1);
	}

	int publicIndexFromWorldAttribute(const QString &value, const int fallbackPublicIndex)
	{
		return publicIndexFromWorldAttribute(value, {}, fallbackPublicIndex);
	}

	int publicIndexFromWorldAttribute(const QString &value, const QVector<QColor> &customTextColours,
	                                  const int fallbackPublicIndex)
	{
		const ParsedNoteColour parsed = parseNoteColourValue(value);
		if (!parsed.valid)
			return clampPublicIndex(fallbackPublicIndex);

		if (parsed.packed == kSameColourPackedValue || parsed.packed < 0)
			return 0;
		if (parsed.packed < MAX_CUSTOM)
			return static_cast<int>(parsed.packed) + 1;
		if (const int legacyIndex = publicIndexFromLegacyRgb(parsed.rgb, customTextColours); legacyIndex >= 0)
			return legacyIndex;
		return clampPublicIndex(fallbackPublicIndex);
	}

	QString worldAttributeFromPublicIndex(const int publicIndex)
	{
		const int  index  = clampPublicIndex(publicIndex);
		const long packed = index == 0 ? kSameColourPackedValue : index - 1;
		return qColorFromColourRef(packed).name(QColor::HexRgb);
	}
} // namespace QMudNoteColour
