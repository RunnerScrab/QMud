/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: AccessibleTextUtils.cpp
 * Role: Accessible text offset mapping helpers for native world output.
 */

#include "AccessibleTextUtils.h"

#include <algorithm>
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

	int saturatedAdd(const int value, const int amount)
	{
		if (amount <= 0)
			return value;
		constexpr int kMaxInt = std::numeric_limits<int>::max();
		if (value >= kMaxInt - amount)
			return kMaxInt;
		return value + amount;
	}

	int clampedOffset(const int offset, const int characterCount)
	{
		return qBound(0, offset, characterCount);
	}
} // namespace

QMudAccessibleTextUtils::LineOffsetMap::LineOffsetMap(QVector<QString> lines)
{
	reset(std::move(lines));
}

void QMudAccessibleTextUtils::LineOffsetMap::reset(QVector<QString> lines)
{
	m_lines = std::move(lines);
	rebuildPrefixes();
}

int QMudAccessibleTextUtils::LineOffsetMap::lineCount() const
{
	return safeQSizeToInt(m_lines.size());
}

int QMudAccessibleTextUtils::LineOffsetMap::characterCount() const
{
	return m_characterCount;
}

bool QMudAccessibleTextUtils::LineOffsetMap::isEmpty() const
{
	return m_characterCount == 0;
}

int QMudAccessibleTextUtils::LineOffsetMap::offsetForPosition(const TextPosition position) const
{
	if (m_lines.isEmpty())
		return 0;
	if (position.line <= 0)
		return qBound(0, position.column, clampedLineLength(0));
	if (position.line >= m_lines.size())
		return m_characterCount;

	const int line   = position.line;
	const int column = qBound(0, position.column, clampedLineLength(line));
	return saturatedAdd(m_lineStarts.at(line), column);
}

QMudAccessibleTextUtils::TextPosition
QMudAccessibleTextUtils::LineOffsetMap::positionForOffset(const int offset) const
{
	if (m_lines.isEmpty())
		return {};

	const int  target = clampedOffset(offset, m_characterCount);
	const auto begin  = m_lineStarts.cbegin();
	const auto end    = m_lineStarts.cend();
	const auto upper  = std::upper_bound(begin, end, target);
	const int  line   = upper == begin ? 0 : static_cast<int>(std::distance(begin, upper) - 1);

	const int  column = qBound(0, target - m_lineStarts.at(line), clampedLineLength(line));
	return TextPosition{line, column};
}

QString QMudAccessibleTextUtils::LineOffsetMap::text(const int startOffset, const int endOffset) const
{
	if (m_lines.isEmpty())
		return {};

	const int start = clampedOffset(qMin(startOffset, endOffset), m_characterCount);
	const int end   = clampedOffset(qMax(startOffset, endOffset), m_characterCount);
	if (end <= start)
		return {};

	QString result;
	result.reserve(end - start);
	for (int line = 0; line < m_lines.size(); ++line)
	{
		const int lineStart = m_lineStarts.at(line);
		if (lineStart >= end)
			break;

		const QString &lineText = m_lines.at(line);
		const int      lineEnd  = saturatedAdd(lineStart, clampedLineLength(line));
		if (start < lineEnd && end > lineStart)
		{
			const int sliceStart = qMax(start, lineStart) - lineStart;
			const int sliceEnd   = qMin(end, lineEnd) - lineStart;
			result += lineText.mid(sliceStart, sliceEnd - sliceStart);
		}

		const bool hasSeparator = line + 1 < m_lines.size();
		if (hasSeparator && start <= lineEnd && end > lineEnd)
			result += QLatin1Char('\n');
	}
	return result;
}

QString QMudAccessibleTextUtils::LineOffsetMap::selectedText(const TextPosition anchor,
                                                             const TextPosition cursor) const
{
	const int anchorOffset = offsetForPosition(anchor);
	const int cursorOffset = offsetForPosition(cursor);
	return text(qMin(anchorOffset, cursorOffset), qMax(anchorOffset, cursorOffset));
}

void QMudAccessibleTextUtils::LineOffsetMap::rebuildPrefixes()
{
	m_lineStarts.clear();
	m_lineStarts.reserve(m_lines.size());

	int offset = 0;
	for (int line = 0; line < m_lines.size(); ++line)
	{
		m_lineStarts.push_back(offset);
		offset = saturatedAdd(offset, clampedLineLength(line));
		if (line + 1 < m_lines.size())
			offset = saturatedAdd(offset, 1);
	}
	m_characterCount = offset;
}

int QMudAccessibleTextUtils::LineOffsetMap::clampedLineLength(const int line) const
{
	if (line < 0 || line >= m_lines.size())
		return 0;
	return safeQSizeToInt(m_lines.at(line).size());
}
