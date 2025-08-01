/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "APIObject.h"
#include <WebCore/ContentExtensionParser.h>
#include <system_error>
#include <wtf/Forward.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
class FragmentedSharedBuffer;
}

namespace API {

class ContentRuleList;

class ContentRuleListStore final : public ObjectImpl<Object::Type::ContentRuleListStore> {
public:
    enum class Error : uint8_t {
        LookupFailed = 1,
        VersionMismatch,
        CompileFailed,
        RemoveFailed
    };

#if ENABLE(CONTENT_EXTENSIONS)
    // This should be incremented every time a functional change is made to the bytecode, file format, etc.
    // to prevent crashing while loading old data.
    static constexpr uint32_t CurrentContentRuleListFileVersion = 20;

    static ContentRuleListStore& defaultStoreSingleton();
    static Ref<ContentRuleListStore> storeWithPath(const WTF::String& storePath);

    explicit ContentRuleListStore();
    explicit ContentRuleListStore(const WTF::String& storePath);
    virtual ~ContentRuleListStore();

    void compileContentRuleList(WTF::String&& identifier, WTF::String&& json, CompletionHandler<void(RefPtr<API::ContentRuleList>, std::error_code)>);
    void lookupContentRuleList(WTF::String&& identifier, CompletionHandler<void(RefPtr<API::ContentRuleList>, std::error_code)>);
    void removeContentRuleList(WTF::String&& identifier, CompletionHandler<void(std::error_code)>);

    void compileContentRuleListFile(WTF::String&& filePath, WTF::String&& identifier, WTF::String&& json, WebCore::ContentExtensions::CSSSelectorsAllowed, CompletionHandler<void(RefPtr<API::ContentRuleList>, std::error_code)>);
    void lookupContentRuleListFile(WTF::String&& filePath, WTF::String&& identifier, CompletionHandler<void(RefPtr<API::ContentRuleList>, std::error_code)>);
    void removeContentRuleListFile(WTF::String&& filePath, CompletionHandler<void(std::error_code)>);

    void getAvailableContentRuleListIdentifiers(CompletionHandler<void(WTF::Vector<WTF::String>)>);

    // For testing only.
    void synchronousRemoveAllContentRuleLists();
    void invalidateContentRuleListVersion(const WTF::String& identifier);
    void corruptContentRuleListHeader(const WTF::String& identifier, bool usingCurrentVersion);
    void corruptContentRuleListActionsMatchingEverything(const WTF::String& identifier);
    void invalidateContentRuleListHeader(const WTF::String& identifier);
    void getContentRuleListSource(WTF::String&& identifier, CompletionHandler<void(WTF::String)>);

    Ref<ConcurrentWorkQueue> protectedCompileQueue();
    Ref<WorkQueue> protectedReadQueue();
    Ref<WorkQueue> protectedRemoveQueue();

private:
    WTF::String defaultStorePath();

    const WTF::String m_storePath;
    const Ref<ConcurrentWorkQueue> m_compileQueue;
    const Ref<WorkQueue> m_readQueue;
    const Ref<WorkQueue> m_removeQueue;
#endif // ENABLE(CONTENT_EXTENSIONS)
};

const std::error_category& contentRuleListStoreErrorCategory();

inline std::error_code make_error_code(ContentRuleListStore::Error error)
{
    return { static_cast<int>(error), contentRuleListStoreErrorCategory() };
}

} // namespace API

SPECIALIZE_TYPE_TRAITS_API_OBJECT(ContentRuleListStore);

namespace std {
template<> struct is_error_code_enum<API::ContentRuleListStore::Error> : public true_type { };
}
