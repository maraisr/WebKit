/*
 * Copyright (C) 2008-2023 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Torch Mobile, Inc. http://www.torchmobile.com/
 * Copyright (C) 2010-2020 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "CSSPreloadScanner.h"

#include "Document.h"
#include <wtf/SetForScope.h>

namespace WebCore {

CSSPreloadScanner::CSSPreloadScanner()
    : m_state(Initial)
    , m_requests(nullptr)
{
}

CSSPreloadScanner::~CSSPreloadScanner() = default;

void CSSPreloadScanner::reset()
{
    m_state = Initial;
    m_rule.clear();
    m_ruleValue.clear();
}

void CSSPreloadScanner::scan(const HTMLToken::DataVector& data, PreloadRequestStream& requests)
{
    ASSERT(!m_requests);
    SetForScope change(m_requests, &requests);

    for (char16_t c : data) {
        if (m_state == DoneParsingImportRules)
            break;

        tokenize(c);
    }

    if (m_state == RuleValue || m_state == AfterRuleValue)
        emitRule();
}

bool CSSPreloadScanner::hasFinishedRuleValue() const
{
    if (m_ruleValue.size() < 2 || m_ruleValue[m_ruleValue.size() - 2] == '\\')
        return false;
    // String
    if (m_ruleValue[0] == '\'' || m_ruleValue[0] == '"')
        return m_ruleValue[0] == m_ruleValue[m_ruleValue.size()  - 1];
    // url()
    return m_ruleValue[m_ruleValue.size() - 1] == ')';
}

inline void CSSPreloadScanner::tokenize(char16_t c)
{
    // We are just interested in @import rules, no need for real tokenization here
    // Searching for other types of resources is probably low payoff.
    switch (m_state) {
    case Initial:
        if (isASCIIWhitespace(c))
            break;
        if (c == '@')
            m_state = RuleStart;
        else if (c == '/')
            m_state = MaybeComment;
        else
            m_state = DoneParsingImportRules;
        break;
    case MaybeComment:
        if (c == '*')
            m_state = Comment;
        else
            m_state = Initial;
        break;
    case Comment:
        if (c == '*')
            m_state = MaybeCommentEnd;
        break;
    case MaybeCommentEnd:
        if (c == '*')
            break;
        if (c == '/')
            m_state = Initial;
        else
            m_state = Comment;
        break;
    case RuleStart:
        if (isASCIIAlpha(c)) {
            m_rule.clear();
            m_ruleValue.clear();
            m_ruleConditions.clear();
            m_rule.append(c);
            m_state = Rule;
        } else
            m_state = Initial;
        break;
    case Rule:
        if (isASCIIWhitespace(c))
            m_state = AfterRule;
        else if (c == ';')
            m_state = Initial;
        else
            m_rule.append(c);
        break;
    case AfterRule:
        if (isASCIIWhitespace(c))
            break;
        if (c == ';')
            m_state = Initial;
        else if (c == '{')
            m_state = DoneParsingImportRules;
        else {
            m_state = RuleValue;
            m_ruleValue.append(c);
        }
        break;
    case RuleValue:
        if (isASCIIWhitespace(c))
            m_state = AfterRuleValue;
        else
            m_ruleValue.append(c);
        if (hasFinishedRuleValue())
            m_state = AfterRuleValue;
        break;
    case AfterRuleValue:
        if (isASCIIWhitespace(c))
            break;
        if (c == ';')
            emitRule();
        else if (c == '{')
            m_state = DoneParsingImportRules;
        else {
            m_state = RuleConditions;
            m_ruleConditions.append(c);
        }
        break;
    case RuleConditions:
        if (c == ';')
            emitRule();
        else if (c == '{')
            m_state = DoneParsingImportRules;
        else
            m_ruleConditions.append(c);
        break;
    case DoneParsingImportRules:
        ASSERT_NOT_REACHED();
        break;
    }
}

static String parseCSSStringOrURL(std::span<const char16_t> characters)
{
    size_t offset = 0;
    size_t reducedLength = characters.size();

    // Remove whitespace from the rule start
    while (reducedLength && isASCIIWhitespace(characters[offset])) {
        ++offset;
        --reducedLength;
    }
    // Remove whitespace from the rule end
    while (reducedLength && isASCIIWhitespace(characters[offset + reducedLength - 1]))
        --reducedLength;

    // Skip the "url(" prefix and the ")" suffix
    if (reducedLength >= 5
            && (characters[offset] == 'u' || characters[offset] == 'U')
            && (characters[offset + 1] == 'r' || characters[offset + 1] == 'R')
            && (characters[offset + 2] == 'l' || characters[offset + 2] == 'L')
            && characters[offset + 3] == '('
            && characters[offset + reducedLength - 1] == ')') {
        offset += 4;
        reducedLength -= 5;
    }

    // Skip whitespace before and after the URL inside the "url()" parenthesis.
    while (reducedLength && isASCIIWhitespace(characters[offset])) {
        ++offset;
        --reducedLength;
    }
    while (reducedLength && isASCIIWhitespace(characters[offset + reducedLength - 1]))
        --reducedLength;
    
    // Remove single-quotes or double-quotes from the URL
    if ((reducedLength >= 2) 
        && (characters[offset] == characters[offset + reducedLength - 1])
        && (characters[offset] == '\'' || characters[offset] == '"')) {
            ++offset;
            reducedLength -= 2;            
        }

    return String(characters.subspan(offset, reducedLength));
}

static bool hasValidImportConditions(StringView conditions)
{
    if (conditions.isEmpty())
        return true;

    conditions = conditions.trim(isASCIIWhitespace<char16_t>);

    // FIXME: Support multiple conditions.
    // FIXME: Support media queries.
    // FIXME: Support supports().

    auto end = conditions.find(')');
    if (end != notFound)
        return end == conditions.length() - 1 && conditions.startsWith("layer("_s);

    return conditions == "layer"_s;
}

void CSSPreloadScanner::emitRule()
{
    StringView rule(m_rule.span());
    if (equalLettersIgnoringASCIICase(rule, "import"_s)) {
        String url = parseCSSStringOrURL(m_ruleValue.span());
        StringView conditions(m_ruleConditions.span());
        if (!url.isEmpty() && hasValidImportConditions(conditions)) {
            URL baseElementURL; // FIXME: This should be passed in from the HTMLPreloadScanner via scan(): without it we will get relative URLs wrong.
            // FIXME: Should this be including the charset in the preload request?
            m_requests->append(makeUnique<PreloadRequest>("css"_s, url, baseElementURL, CachedResource::Type::CSSStyleSheet, String(), ScriptType::Classic, ReferrerPolicy::EmptyString));
        }
        m_state = Initial;
    } else if (equalLettersIgnoringASCIICase(rule, "charset"_s))
        m_state = Initial;
    else
        m_state = DoneParsingImportRules;
    m_rule.clear();
    m_ruleValue.clear();
    m_ruleConditions.clear();
}

}
