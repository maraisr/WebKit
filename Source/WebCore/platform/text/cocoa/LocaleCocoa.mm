/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"
#import "LocaleCocoa.h"

#import "DateComponents.h"
#import "LocalizedStrings.h"
#import <Foundation/NSDateFormatter.h>
#import <Foundation/NSLocale.h>
#import <wtf/DateMath.h>
#import <wtf/HashMap.h>
#import <wtf/Language.h>
#import <wtf/RetainPtr.h>
#import <wtf/TZoneMallocInlines.h>
#import <wtf/cocoa/TypeCastsCocoa.h>
#import <wtf/cocoa/VectorCocoa.h>
#import <wtf/text/AtomStringHash.h>
#import "LocalizedDateCache.h"

#if PLATFORM(IOS_FAMILY)
#import <UIKit/UIKit.h>
#import <pal/ios/UIKitSoftLink.h>
#endif

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(LocaleCocoa);

std::unique_ptr<Locale> Locale::create(const AtomString& locale)
{
    return makeUnique<LocaleCocoa>(locale);
}

static RetainPtr<NSDateFormatter> createDateTimeFormatter(NSLocale* locale, NSCalendar* calendar, NSDateFormatterStyle dateStyle, NSDateFormatterStyle timeStyle)
{
    auto formatter = adoptNS([[NSDateFormatter alloc] init]);
    [formatter setLocale:locale];
    [formatter setDateStyle:dateStyle];
    [formatter setTimeStyle:timeStyle];
    [formatter setTimeZone:[NSTimeZone timeZoneWithAbbreviation:@"UTC"]];
    [formatter setCalendar:calendar];
    return formatter;
}

LocaleCocoa::LocaleCocoa(const AtomString& locale)
    : m_locale(adoptNS([[NSLocale alloc] initWithLocaleIdentifier:locale.createNSString().get()]))
    , m_gregorianCalendar(adoptNS([[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian]))
    , m_didInitializeNumberData(false)
{
    NSArray* availableLanguages = [NSLocale ISOLanguageCodes];
    // NSLocale returns a lower case NSLocaleLanguageCode so we don't have care about case.
    NSString* language = [m_locale objectForKey:NSLocaleLanguageCode];
    if ([availableLanguages indexOfObject:language] == NSNotFound)
        m_locale = adoptNS([[NSLocale alloc] initWithLocaleIdentifier:defaultLanguage().createNSString().get()]);
    [m_gregorianCalendar setLocale:m_locale.get()];
}

LocaleCocoa::~LocaleCocoa()
{
}

RetainPtr<NSDateFormatter> LocaleCocoa::shortDateFormatter()
{
    return createDateTimeFormatter(m_locale.get(), m_gregorianCalendar.get(), NSDateFormatterShortStyle, NSDateFormatterNoStyle);
}

String LocaleCocoa::formatDateTime(const DateComponents& dateComponents, FormatType)
{
    double msec = dateComponents.millisecondsSinceEpoch();
    DateComponentsType type = dateComponents.type();

    ASSERT(type != DateComponentsType::Invalid);

    if (type == DateComponentsType::Week) {
#if ENABLE(INPUT_TYPE_WEEK_PICKER)
        // NSDateFormatter is not used here because it handles week numbering differently than ISO-8601.
        return inputWeekLabel(dateComponents);
#else
        return String();
#endif
    }

    // Incoming msec value is milliseconds since 1970-01-01 00:00:00 UTC. The 1970 epoch.
    NSTimeInterval secondsSince1970 = (msec / 1000);
    NSDate *date = [NSDate dateWithTimeIntervalSince1970:secondsSince1970];

    // Return a formatted string.
    NSDateFormatter *dateFormatter = localizedDateCache().formatterForDateType(type);
    return [dateFormatter stringFromDate:date];
}

const Vector<String>& LocaleCocoa::monthLabels()
{
    if (!m_monthLabels.isEmpty())
        return m_monthLabels;
    NSArray *array = [shortDateFormatter().get() monthSymbols];
    if ([array count] == 12) {
        m_monthLabels = makeVector<String>(array);
        return m_monthLabels;
    }
    m_monthLabels = std::span { WTF::monthFullName };
    return m_monthLabels;
}

RetainPtr<NSDateFormatter> LocaleCocoa::timeFormatter()
{
    return createDateTimeFormatter(m_locale.get(), m_gregorianCalendar.get(), NSDateFormatterNoStyle, NSDateFormatterMediumStyle);
}

RetainPtr<NSDateFormatter> LocaleCocoa::shortTimeFormatter()
{
    return createDateTimeFormatter(m_locale.get(), m_gregorianCalendar.get(), NSDateFormatterNoStyle, NSDateFormatterShortStyle);
}

RetainPtr<NSDateFormatter> LocaleCocoa::dateTimeFormatterWithSeconds()
{
    return createDateTimeFormatter(m_locale.get(), m_gregorianCalendar.get(), NSDateFormatterShortStyle, NSDateFormatterMediumStyle);
}

RetainPtr<NSDateFormatter> LocaleCocoa::dateTimeFormatterWithoutSeconds()
{
    return createDateTimeFormatter(m_locale.get(), m_gregorianCalendar.get(), NSDateFormatterShortStyle, NSDateFormatterShortStyle);
}

Locale::WritingDirection LocaleCocoa::defaultWritingDirection() const
{
    switch ([PlatformNSParagraphStyle defaultWritingDirectionForLanguage:m_locale.get().languageCode]) {
    case NSWritingDirectionLeftToRight:
        return WritingDirection::LeftToRight;
    case NSWritingDirectionRightToLeft:
        return WritingDirection::RightToLeft;
    default:
        return WritingDirection::Default;
    }
}

String LocaleCocoa::dateFormat()
{
    if (!m_dateFormat.isNull())
        return m_dateFormat;
    m_dateFormat = [shortDateFormatter().get() dateFormat];
    return m_dateFormat;
}

String LocaleCocoa::monthFormat()
{
    if (!m_monthFormat.isNull())
        return m_monthFormat;
    // Gets a format for "MMMM" because Windows API always provides formats for
    // "MMMM" in some locales.
    m_monthFormat = [NSDateFormatter dateFormatFromTemplate:@"yyyyMMMM" options:0 locale:m_locale.get()];
    return m_monthFormat;
}

String LocaleCocoa::shortMonthFormat()
{
    if (!m_shortMonthFormat.isNull())
        return m_shortMonthFormat;
    m_shortMonthFormat = [NSDateFormatter dateFormatFromTemplate:@"yM" options:0 locale:m_locale.get()];
    return m_shortMonthFormat;
}

String LocaleCocoa::timeFormat()
{
    if (!m_timeFormatWithSeconds.isNull())
        return m_timeFormatWithSeconds;
    m_timeFormatWithSeconds = [timeFormatter().get() dateFormat];
    return m_timeFormatWithSeconds;
}

String LocaleCocoa::shortTimeFormat()
{
    if (!m_timeFormatWithoutSeconds.isNull())
        return m_timeFormatWithoutSeconds;
    m_timeFormatWithoutSeconds = [shortTimeFormatter().get() dateFormat];
    return m_timeFormatWithoutSeconds;
}

String LocaleCocoa::dateTimeFormatWithSeconds()
{
    if (!m_dateTimeFormatWithSeconds.isNull())
        return m_dateTimeFormatWithSeconds;
    m_dateTimeFormatWithSeconds = [dateTimeFormatterWithSeconds().get() dateFormat];
    return m_dateTimeFormatWithSeconds;
}

String LocaleCocoa::dateTimeFormatWithoutSeconds()
{
    if (!m_dateTimeFormatWithoutSeconds.isNull())
        return m_dateTimeFormatWithoutSeconds;
    m_dateTimeFormatWithoutSeconds = [dateTimeFormatterWithoutSeconds().get() dateFormat];
    return m_dateTimeFormatWithoutSeconds;
}

const Vector<String>& LocaleCocoa::shortMonthLabels()
{
    if (!m_shortMonthLabels.isEmpty())
        return m_shortMonthLabels;
    NSArray *array = [shortDateFormatter().get() shortMonthSymbols];
    if ([array count] == 12) {
        m_shortMonthLabels = makeVector<String>(array);
        return m_shortMonthLabels;
    }
    m_shortMonthLabels = std::span { WTF::monthName };
    return m_shortMonthLabels;
}

const Vector<String>& LocaleCocoa::standAloneMonthLabels()
{
    if (!m_standAloneMonthLabels.isEmpty())
        return m_standAloneMonthLabels;
    NSArray *array = [shortDateFormatter().get() standaloneMonthSymbols];
    if ([array count] == 12) {
        m_standAloneMonthLabels = makeVector<String>(array);
        return m_standAloneMonthLabels;
    }
    m_standAloneMonthLabels = shortMonthLabels();
    return m_standAloneMonthLabels;
}

const Vector<String>& LocaleCocoa::shortStandAloneMonthLabels()
{
    if (!m_shortStandAloneMonthLabels.isEmpty())
        return m_shortStandAloneMonthLabels;

    NSArray *array = [shortDateFormatter().get() shortStandaloneMonthSymbols];
    if ([array count] == 12) {
        m_shortStandAloneMonthLabels = makeVector<String>(array);
        return m_shortStandAloneMonthLabels;
    }
    m_shortStandAloneMonthLabels = shortMonthLabels();
    return m_shortStandAloneMonthLabels;
}

const Vector<String>& LocaleCocoa::timeAMPMLabels()
{
    if (!m_timeAMPMLabels.isEmpty())
        return m_timeAMPMLabels;
    RetainPtr<NSDateFormatter> formatter = shortTimeFormatter();
    m_timeAMPMLabels = { String([formatter AMSymbol]), String([formatter PMSymbol]) };
    return m_timeAMPMLabels;
}

using CanonicalLocaleMap = HashMap<AtomString, RetainPtr<CFStringRef>>;

struct LocaleCache {
    AtomString m_key;
    RetainPtr<CFStringRef> m_value;
    CanonicalLocaleMap m_map;
};


static LocaleCache& localeCache()
{
    static MainThreadNeverDestroyed<LocaleCache> localeCache;
    return localeCache.get();
}

RetainPtr<CFStringRef> LocaleCocoa::canonicalLanguageIdentifierFromString(const AtomString& string)
{
    if (string.isEmpty())
        return CFSTR("");

    auto& cache = localeCache();
    if (cache.m_key == string)
        return cache.m_value;
    auto result = cache.m_map.ensure(string, [&] {
        return bridge_cast([NSLocale canonicalLanguageIdentifierFromString:string.createNSString().get()]);
    }).iterator->value;
    cache.m_key = string;
    cache.m_value = result;
    return result;
}

void LocaleCocoa::releaseMemory()
{
    auto& cache = localeCache();
    cache.m_key = { };
    cache.m_value = { };
    cache.m_map.clear();
}

void LocaleCocoa::initializeLocaleData()
{
    if (m_didInitializeNumberData)
        return;
    m_didInitializeNumberData = true;

    RetainPtr<NSNumberFormatter> formatter = adoptNS([[NSNumberFormatter alloc] init]);
    [formatter setLocale:m_locale.get()];
    [formatter setNumberStyle:NSNumberFormatterDecimalStyle];
    [formatter setUsesGroupingSeparator:NO];

    RetainPtr<NSNumber> sampleNumber = adoptNS([[NSNumber alloc] initWithDouble:9876543210]);
    String nineToZero([formatter stringFromNumber:sampleNumber.get()]);
    if (nineToZero.length() != 10)
        return;
    Vector<String, DecimalSymbolsSize> symbols;
    for (unsigned i = 0; i < 10; ++i)
        symbols.append(nineToZero.substring(9 - i, 1));
    ASSERT(symbols.size() == DecimalSeparatorIndex);
    symbols.append([formatter decimalSeparator]);
    ASSERT(symbols.size() == GroupSeparatorIndex);
    symbols.append([formatter groupingSeparator]);
    ASSERT(symbols.size() == DecimalSymbolsSize);

    String positivePrefix([formatter positivePrefix]);
    String positiveSuffix([formatter positiveSuffix]);
    String negativePrefix([formatter negativePrefix]);
    String negativeSuffix([formatter negativeSuffix]);
    setLocaleData(symbols, positivePrefix, positiveSuffix, negativePrefix, negativeSuffix);
}

}
