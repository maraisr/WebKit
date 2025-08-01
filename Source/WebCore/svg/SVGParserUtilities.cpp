/*
 * Copyright (C) 2002, 2003 The Karbon Developers
 * Copyright (C) 2006 Alexander Kellett <lypanov@kde.org>
 * Copyright (C) 2006, 2007 Rob Buis <buis@kde.org>
 * Copyright (C) 2007-2024 Apple Inc. All rights reserved.
 * Copyright (C) 2016-2018 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "SVGParserUtilities.h"

#include "Document.h"
#include "FloatRect.h"
#include <limits>
#include <wtf/ASCIICType.h>
#include <wtf/text/StringParsingBuffer.h>
#include <wtf/text/StringView.h>

namespace WebCore {

template <typename FloatType> static inline bool isValidRange(const FloatType x)
{
    static const FloatType max = std::numeric_limits<FloatType>::max();
    return x >= -max && x <= max;
}

// We use this generic parseNumber function to allow the Path parsing code to work 
// at a higher precision internally, without any unnecessary runtime cost or code
// complexity.
// FIXME: Can this be shared/replaced with number parsing in WTF?
template <typename CharacterType, typename FloatType = float> static std::optional<FloatType> genericParseNumber(StringParsingBuffer<CharacterType>& buffer, SuffixSkippingPolicy skip = SuffixSkippingPolicy::Skip)
{
    // read the sign
    int sign = 1;
    if (buffer.hasCharactersRemaining() && *buffer == '+')
        ++buffer;
    else if (buffer.hasCharactersRemaining() && *buffer == '-') {
        ++buffer;
        sign = -1;
    } 
    
    if (buffer.atEnd() || (!isASCIIDigit(*buffer) && *buffer != '.'))
        return std::nullopt;

    // read the integer part, build right-to-left
    auto spanDigitsStart = buffer.span();
    
    // Advance to first non-digit.
    skipWhile<isASCIIDigit>(buffer);

    FloatType integer = 0;
    if (buffer.position() > spanDigitsStart.data()) {
        size_t indexScanIntPart = buffer.position() - spanDigitsStart.data();
        FloatType multiplier = 1;
        for (size_t i = indexScanIntPart; i > 0; --i) {
            integer += multiplier * static_cast<FloatType>(spanDigitsStart[i - 1] - '0');
            multiplier *= 10;
        }
        // Bail out early if this overflows.
        if (!isValidRange(integer))
            return std::nullopt;
    }

    // read the decimals
    FloatType decimal = 0;
    if (buffer.hasCharactersRemaining() && *buffer == '.') {
        ++buffer;
        
        // There must be a least one digit following the .
        if (buffer.atEnd() || !isASCIIDigit(*buffer))
            return std::nullopt;
        
        FloatType frac = 1;
        while (buffer.hasCharactersRemaining() && isASCIIDigit(*buffer)) {
            frac *= static_cast<FloatType>(0.1);
            decimal += (*(buffer++) - '0') * frac;
        }
    }

    // When we get here we should have consumed either a digit for the integer
    // part or a fractional part (with at least one digit after the '.'.)
    ASSERT(spanDigitsStart.data() != buffer.position());

    FloatType number = 0;
    number = integer + decimal;
    number *= sign;

    // read the exponent part
    if (buffer.span().size() > 1 && (*buffer == 'e' || *buffer == 'E')
        && (buffer[1] != 'x' && buffer[1] != 'm')) {
        ++buffer;

        // read the sign of the exponent
        bool exponentIsNegative = false;
        if (*buffer == '+')
            ++buffer;
        else if (*buffer == '-') {
            ++buffer;
            exponentIsNegative = true;
        }

        // There must be an exponent
        if (buffer.atEnd() || !isASCIIDigit(*buffer))
            return std::nullopt;

        FloatType exponent = 0;
        while (buffer.hasCharactersRemaining() && isASCIIDigit(*buffer)) {
            exponent *= static_cast<FloatType>(10);
            exponent += *buffer++ - '0';
        }

        // FIXME: This is unnecessairly strict - the position of the decimal point
        // is not taken into account when limiting 'exponent'.
        if (exponentIsNegative)
            exponent = -exponent;
        // Fail if the exponent is greater than the largest positive power of ten
        // (that would yield a representable float).
        if (exponent > std::numeric_limits<FloatType>::max_exponent10)
            return false;

        // If the exponent is smaller than smallest negative power of 10 (that
        // would yield a representable float), then rely on the pow()+rounding to
        // produce a reasonable result (likely zero.)
        if (exponent)
            number *= static_cast<FloatType>(std::pow(10.0, exponent));
    }

    // Don't return Infinity() or NaN().
    if (!isValidRange(number))
        return std::nullopt;

    if (skip == SuffixSkippingPolicy::Skip)
        skipOptionalSVGSpacesOrDelimiter(buffer);

    return number;
}

std::optional<float> parseNumber(StringParsingBuffer<LChar>& buffer, SuffixSkippingPolicy skip)
{
    return genericParseNumber(buffer, skip);
}

std::optional<float> parseNumber(StringParsingBuffer<char16_t>& buffer, SuffixSkippingPolicy skip)
{
    return genericParseNumber(buffer, skip);
}

std::optional<float> parseNumber(StringView string, SuffixSkippingPolicy skip)
{
    return readCharactersForParsing(string, [skip](auto buffer) -> std::optional<float> {
        auto result = genericParseNumber(buffer, skip);
        if (!buffer.atEnd())
            return std::nullopt;
        return result;
    });
}

// only used to parse largeArcFlag and sweepFlag which must be a "0" or "1"
// and might not have any whitespace/comma after it
template <typename CharacterType> std::optional<bool> genericParseArcFlag(StringParsingBuffer<CharacterType>& buffer)
{
    if (buffer.atEnd())
        return std::nullopt;

    const CharacterType flagChar = *buffer;
    ++buffer;

    bool flag;
    if (flagChar == '0')
        flag = false;
    else if (flagChar == '1')
        flag = true;
    else
        return std::nullopt;

    skipOptionalSVGSpacesOrDelimiter(buffer);
    
    return flag;
}

std::optional<bool> parseArcFlag(StringParsingBuffer<LChar>& buffer)
{
    return genericParseArcFlag(buffer);
}

std::optional<bool> parseArcFlag(StringParsingBuffer<char16_t>& buffer)
{
    return genericParseArcFlag(buffer);
}

std::optional<std::pair<float, float>> parseNumberOptionalNumber(StringView string)
{
    if (string.isEmpty())
        return std::nullopt;

    return readCharactersForParsing(string, [](auto buffer) -> std::optional<std::pair<float, float>> {
        auto x = parseNumber(buffer);
        if (!x)
            return std::nullopt;

        if (buffer.atEnd())
            return std::make_pair(*x, *x);

        auto y = parseNumber(buffer, SuffixSkippingPolicy::DontSkip);
        if (!y)
            return std::nullopt;

        if (!buffer.atEnd())
            return std::nullopt;

        return std::make_pair(*x, *y);
    });
}

std::optional<FloatPoint> parsePoint(StringView string)
{
    if (string.isEmpty())
        return std::nullopt;

    return readCharactersForParsing(string, [](auto buffer) -> std::optional<FloatPoint> {
        if (!skipOptionalSVGSpaces(buffer))
            return std::nullopt;

        auto point = parseFloatPoint(buffer);
        if (!point)
            return std::nullopt;

        // Disallow anything except spaces at the end.
        skipOptionalSVGSpaces(buffer);
        
        return point;
    });
}

std::optional<FloatRect> parseRect(StringView string)
{
    return readCharactersForParsing(string, [](auto buffer) -> std::optional<FloatRect> {
        skipOptionalSVGSpaces(buffer);
        
        auto x = parseNumber(buffer);
        if (!x)
            return std::nullopt;
        auto y = parseNumber(buffer);
        if (!y)
            return std::nullopt;
        auto width = parseNumber(buffer);
        if (!width)
            return std::nullopt;
        auto height = parseNumber(buffer, SuffixSkippingPolicy::DontSkip);
        if (!height)
            return std::nullopt;

        return FloatRect { *x, *y, *width, *height };
    });
}

std::optional<HashSet<String>> parseGlyphName(StringView string)
{
    // FIXME: Parsing error detection is missing.

    return readCharactersForParsing(string, [](auto buffer) {
        skipOptionalSVGSpaces(buffer);

        HashSet<String> values;

        while (buffer.hasCharactersRemaining()) {
            // Leading and trailing white space, and white space before and after separators, will be ignored.
            auto inputStart = buffer.span();

            skipUntil(buffer, ',');

            if (buffer.position() == inputStart.data())
                break;

            // Walk backwards from the ; to ignore any whitespace.
            size_t index = buffer.position() - inputStart.data();
            while (index > 0 && isASCIIWhitespace(inputStart[index - 1]))
                --index;

            values.add(inputStart.first(index));
            skipOptionalSVGSpacesOrDelimiter(buffer, ',');
        }
        return values;
    });
}

template<typename CharacterType> static std::optional<UnicodeRange> parseUnicodeRange(std::span<const CharacterType> span)
{
    StringParsingBuffer buffer { span };
    unsigned length = buffer.lengthRemaining();
    if (length < 2 || buffer[0] != 'U' || buffer[1] != '+')
        return std::nullopt;

    buffer += 2;

    // Parse the starting hex number (or its prefix).
    unsigned startRange = 0;
    unsigned startLength = 0;

    while (buffer.hasCharactersRemaining()) {
        if (!isASCIIHexDigit(*buffer))
            break;
        ++startLength;
        if (startLength > 6)
            return std::nullopt;
        startRange = (startRange << 4) | toASCIIHexValue(*buffer);
        ++buffer;
    }

    // Handle the case of ranges separated by "-" sign.
    if (2 + startLength < length && *buffer == '-') {
        if (!startLength)
            return std::nullopt;
        
        // Parse the ending hex number (or its prefix).
        unsigned endRange = 0;
        unsigned endLength = 0;
        ++buffer;
        while (buffer.hasCharactersRemaining()) {
            if (!isASCIIHexDigit(*buffer))
                break;
            ++endLength;
            if (endLength > 6)
                return std::nullopt;
            endRange = (endRange << 4) | toASCIIHexValue(*buffer);
            ++buffer;
        }
        
        if (!endLength)
            return std::nullopt;
        
        UnicodeRange range;
        range.first = startRange;
        range.second = endRange;
        return range;
    }
    
    // Handle the case of a number with some optional trailing question marks.
    unsigned endRange = startRange;
    while (buffer.hasCharactersRemaining()) {
        if (*buffer != '?')
            break;
        ++startLength;
        if (startLength > 6)
            return std::nullopt;
        startRange <<= 4;
        endRange = (endRange << 4) | 0xF;
        ++buffer;
    }
    
    if (!startLength)
        return std::nullopt;
    
    UnicodeRange range;
    range.first = startRange;
    range.second = endRange;
    return range;
}

std::optional<std::pair<UnicodeRanges, HashSet<String>>> parseKerningUnicodeString(StringView string)
{
    // FIXME: Parsing error detection is missing.

    return readCharactersForParsing(string, [](auto buffer) -> std::pair<UnicodeRanges, HashSet<String>> {
        UnicodeRanges rangeList;
        HashSet<String> stringList;

        while (1) {
            auto inputStart = buffer.position();

            skipUntil(buffer, ',');

            if (buffer.position() == inputStart)
                break;

            // Try to parse unicode range first
            if (auto range = parseUnicodeRange(std::span { inputStart, buffer.position() }))
                rangeList.append(WTFMove(*range));
            else
                stringList.add(String({ inputStart, buffer.position() }));

            if (buffer.atEnd())
                break;

            ++buffer;
        }

        return std::make_pair(WTFMove(rangeList), WTFMove(stringList));
    });
}

template <typename CharacterType> static std::optional<FloatPoint> genericParseFloatPoint(StringParsingBuffer<CharacterType>& buffer)
{
    auto x = parseNumber(buffer);
    if (!x)
        return std::nullopt;

    auto y = parseNumber(buffer);
    if (!y)
        return std::nullopt;

    return FloatPoint { *x, *y };
}

std::optional<FloatPoint> parseFloatPoint(StringParsingBuffer<LChar>& buffer)
{
    return genericParseFloatPoint(buffer);
}

std::optional<FloatPoint> parseFloatPoint(StringParsingBuffer<char16_t>& buffer)
{
    return genericParseFloatPoint(buffer);
}

}
