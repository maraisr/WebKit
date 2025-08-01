/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 * Copyright (C) 2015-2024 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
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

#include "config.h"
#include "ShadowRoot.h"

#include "AXObjectCache.h"
#include "CSSStyleSheet.h"
#include "ChildListMutationScope.h"
#include "ContainerNodeInlines.h"
#include "CustomElementRegistry.h"
#include "ElementInlines.h"
#include "ElementTraversal.h"
#include "ExceptionCode.h"
#include "GetHTMLOptions.h"
#include "HTMLSlotElement.h"
#if ENABLE(PICTURE_IN_PICTURE_API)
#include "NotImplemented.h"
#endif
#include "RenderElement.h"
#include "SerializedNode.h"
#include "SlotAssignment.h"
#include "StyleResolver.h"
#include "StyleScope.h"
#include "StyleSheetList.h"
#include "TrustedType.h"
#include "WebAnimation.h"
#include "markup.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(ShadowRoot);

struct SameSizeAsShadowRoot : public DocumentFragment, public TreeScope {
    uint8_t flagsAndModes[3];
    WeakPtr<Element, WeakPtrImplWithEventTargetData> host;
    void* styleSheetList;
    void* styleScope;
    void* slotAssignment;
    std::optional<HashMap<AtomString, AtomString>> partMappings;
    AtomString referenceTarget;
};

static_assert(sizeof(ShadowRoot) == sizeof(SameSizeAsShadowRoot), "shadowroot should stay small");
#if !ASSERT_ENABLED
static_assert(sizeof(WeakPtr<Element, WeakPtrImplWithEventTargetData>) == sizeof(void*), "WeakPtr should be same size as raw pointer");
#endif

ShadowRoot::ShadowRoot(Document& document, ShadowRootMode mode, SlotAssignmentMode assignmentMode, DelegatesFocus delegatesFocus, Clonable clonable, Serializable serializable, AvailableToElementInternals availableToElementInternals, RefPtr<CustomElementRegistry>&& registry, ScopedCustomElementRegistry scopedRegistry, const AtomString& referenceTarget)
    : DocumentFragment(document, TypeFlag::IsShadowRootOrFormControlElement)
    , TreeScope(*this, document, WTFMove(registry))
    , m_delegatesFocus(delegatesFocus == DelegatesFocus::Yes)
    , m_isClonable(clonable == Clonable::Yes)
    , m_serializable(serializable == Serializable::Yes)
    , m_availableToElementInternals(availableToElementInternals == AvailableToElementInternals::Yes)
    , m_hasScopedCustomElementRegistry(scopedRegistry == ScopedCustomElementRegistry::Yes)
    , m_mode(mode)
    , m_slotAssignmentMode(assignmentMode)
    , m_styleScope(makeUnique<Style::Scope>(*this))
    , m_referenceTarget(referenceTarget)
{
    setEventTargetFlag(EventTargetFlag::IsInShadowTree);
    if (m_mode == ShadowRootMode::UserAgent)
        setEventTargetFlag(EventTargetFlag::HasBeenInUserAgentShadowTree);
}


ShadowRoot::ShadowRoot(Document& document, std::unique_ptr<SlotAssignment>&& slotAssignment)
    : DocumentFragment(document, TypeFlag::IsShadowRootOrFormControlElement)
    , TreeScope(*this, document, nullptr)
    , m_mode(ShadowRootMode::UserAgent)
    , m_styleScope(makeUnique<Style::Scope>(*this))
    , m_slotAssignment(WTFMove(slotAssignment))
{
    setEventTargetFlag(EventTargetFlag::IsInShadowTree);
    setEventTargetFlag(EventTargetFlag::HasBeenInUserAgentShadowTree);
}


ShadowRoot::~ShadowRoot()
{
    if (isConnected())
        document().didRemoveInDocumentShadowRoot(*this);

    if (RefPtr styleSheetList = m_styleSheetList)
        styleSheetList->detach();

    // We cannot let ContainerNode destructor call willBeDeletedFrom()
    // for this ShadowRoot instance because TreeScope destructor
    // clears Node::m_treeScope thus ContainerNode is no longer able
    // to access it Document reference after that.
    // We can't ref document() here since it may have started destruction.
    willBeDeletedFrom(document());

    ASSERT(!m_hasBegunDeletingDetachedChildren);
    m_hasBegunDeletingDetachedChildren = true;

    // We must remove all of our children first before the TreeScope destructor
    // runs so we don't go through Node::setTreeScopeRecursively for each child with a
    // destructed tree scope in each descendant.
    removeDetachedChildren();
}

Node::InsertedIntoAncestorResult ShadowRoot::insertedIntoAncestor(InsertionType insertionType, ContainerNode& parentOfInsertedTree)
{
    DocumentFragment::insertedIntoAncestor(insertionType, parentOfInsertedTree);
    if (!hasScopedCustomElementRegistry() && usesNullCustomElementRegistry() && !parentOfInsertedTree.usesNullCustomElementRegistry()) {
        if (RefPtr registry = CustomElementRegistry::registryForElement(*host())) {
            clearUsesNullCustomElementRegistry();
            setCustomElementRegistry(*registry);
        }
    }
    if (insertionType.connectedToDocument) {
        protectedDocument()->didInsertInDocumentShadowRoot(*this);
        if (m_hasScopedCustomElementRegistry) {
            if (RefPtr registry = customElementRegistry())
                registry->didAssociateWithDocument(protectedDocument());
        }
    }
    if (!adoptedStyleSheets().empty() && document().frame())
        checkedStyleScope()->didChangeActiveStyleSheetCandidates();
    return InsertedIntoAncestorResult::Done;
}

CheckedRef<Style::Scope> ShadowRoot::checkedStyleScope() const
{
    return *m_styleScope;
}

void ShadowRoot::removedFromAncestor(RemovalType removalType, ContainerNode& oldParentOfRemovedTree)
{
    DocumentFragment::removedFromAncestor(removalType, oldParentOfRemovedTree);
    if (removalType.disconnectedFromDocument)
        protectedDocument()->didRemoveInDocumentShadowRoot(*this);
}

void ShadowRoot::childrenChanged(const ChildChange& childChange)
{
    DocumentFragment::childrenChanged(childChange);

    if (!m_host || m_mode == ShadowRootMode::UserAgent)
        return; // Don't support first-child, nth-of-type, etc... in UA shadow roots as an optimization.

    // FIXME: Avoid always invalidating style just for first-child, etc... as done in Element::childrenChanged.
    switch (childChange.type) {
    case ChildChange::Type::ElementInserted:
    case ChildChange::Type::ElementRemoved:
        m_host->invalidateStyleForSubtreeInternal();
        break;
    case ChildChange::Type::TextInserted:
    case ChildChange::Type::TextRemoved:
    case ChildChange::Type::TextChanged:
    case ChildChange::Type::AllChildrenRemoved:
    case ChildChange::Type::NonContentsChildRemoved:
    case ChildChange::Type::NonContentsChildInserted:
    case ChildChange::Type::AllChildrenReplaced:
        break;
    }
}

void ShadowRoot::moveShadowRootToNewParentScope(TreeScope& newScope, Document& newDocument)
{
    Ref oldDocument = documentScope();
    setParentTreeScope(newScope);
    moveShadowRootToNewDocument(oldDocument, newDocument);
}

void ShadowRoot::moveShadowRootToNewDocument(Document& oldDocument, Document& newDocument)
{
    if (oldDocument.templateDocumentHost() != &newDocument && newDocument.templateDocumentHost() != &oldDocument)
        setAdoptedStyleSheets({ });

    setDocumentScope(newDocument);
    RELEASE_ASSERT_WITH_SECURITY_IMPLICATION(!parentTreeScope() || &parentTreeScope()->documentScope() == &newDocument);

    // Style scopes are document specific.
    m_styleScope = makeUnique<Style::Scope>(*this);
    RELEASE_ASSERT_WITH_SECURITY_IMPLICATION(&m_styleScope->document() == &newDocument);
}

StyleSheetList& ShadowRoot::styleSheets()
{
    if (!m_styleSheetList)
        m_styleSheetList = StyleSheetList::create(*this);
    return *m_styleSheetList;
}

CustomElementRegistry* ShadowRoot::registryForBindings() const
{
    if (usesNullCustomElementRegistry())
        return nullptr;
    auto* registry = customElementRegistry();
    if (RefPtr window = document().window(); window && !registry)
        registry = &window->ensureCustomElementRegistry();
    return registry;
}

ExceptionOr<void> ShadowRoot::replaceChildrenWithMarkup(const String& markup, OptionSet<ParserContentPolicy> parserContentPolicy)
{
    auto policy = OptionSet<ParserContentPolicy> { ParserContentPolicy::AllowScriptingContent } | parserContentPolicy;

    if (markup.isEmpty()) {
        ChildListMutationScope mutation(*this);
        removeChildren();
        return { };
    }

    auto fragment = createFragmentForInnerOuterHTML(*protectedHost(), markup, policy, customElementRegistry());
    if (fragment.hasException())
        return fragment.releaseException();
    return replaceChildrenWithFragment(*this, fragment.releaseReturnValue());
}

ExceptionOr<void> ShadowRoot::setHTMLUnsafe(Variant<RefPtr<TrustedHTML>, String>&& html)
{
    auto stringValueHolder = trustedTypeCompliantString(*document().scriptExecutionContext(), WTFMove(html), "ShadowRoot setHTMLUnsafe"_s);

    if (stringValueHolder.hasException())
        return stringValueHolder.releaseException();

    return replaceChildrenWithMarkup(stringValueHolder.releaseReturnValue(), { ParserContentPolicy::AllowDeclarativeShadowRoots, ParserContentPolicy::AlwaysParseAsHTML });
}

String ShadowRoot::getHTML(GetHTMLOptions&& options) const
{
    return serializeFragment(*this, SerializedNodes::SubtreesOfChildren, nullptr, ResolveURLs::NoExcludingURLsForPrivacy, SerializationSyntax::HTML, options.serializableShadowRoots ? SerializeShadowRoots::Serializable : SerializeShadowRoots::Explicit, WTFMove(options.shadowRoots));
}

String ShadowRoot::innerHTML() const
{
    return serializeFragment(*this, SerializedNodes::SubtreesOfChildren, nullptr, ResolveURLs::NoExcludingURLsForPrivacy);
}

ExceptionOr<void> ShadowRoot::setInnerHTML(Variant<RefPtr<TrustedHTML>, String>&& html)
{
    auto stringValueHolder = trustedTypeCompliantString(*document().scriptExecutionContext(), WTFMove(html), "ShadowRoot innerHTML"_s);

    if (stringValueHolder.hasException())
        return stringValueHolder.releaseException();

    return replaceChildrenWithMarkup(stringValueHolder.releaseReturnValue(), { });
}

bool ShadowRoot::childTypeAllowed(NodeType type) const
{
    switch (type) {
    case ELEMENT_NODE:
    case PROCESSING_INSTRUCTION_NODE:
    case COMMENT_NODE:
    case TEXT_NODE:
    case CDATA_SECTION_NODE:
        return true;
    default:
        return false;
    }
}

Ref<Node> ShadowRoot::cloneNodeInternal(Document& document, CloningOperation type, CustomElementRegistry* registry) const
{
    RELEASE_ASSERT(m_mode != ShadowRootMode::UserAgent);
    ASSERT(m_isClonable);
    switch (type) {
    case CloningOperation::SelfWithTemplateContent:
        return create(document, m_mode, m_slotAssignmentMode,
            m_delegatesFocus ? DelegatesFocus::Yes : DelegatesFocus::No,
            Clonable::Yes,
            m_serializable ? Serializable::Yes : Serializable::No,
            m_availableToElementInternals ? AvailableToElementInternals::Yes : AvailableToElementInternals::No,
            registry,
            m_hasScopedCustomElementRegistry ? ScopedCustomElementRegistry::Yes : ScopedCustomElementRegistry::No);
    case CloningOperation::SelfOnly:
    case CloningOperation::Everything:
        break;
    }

    RELEASE_ASSERT_NOT_REACHED(); // ShadowRoot is never cloned directly on its own.
}

SerializedNode ShadowRoot::serializeNode(CloningOperation) const
{
    // FIXME: Implement.
    return { SerializedNode::ShadowRoot { } };
}

void ShadowRoot::removeAllEventListeners()
{
    DocumentFragment::removeAllEventListeners();
    for (RefPtr node = firstChild(); node; node = NodeTraversal::next(*node))
        node->removeAllEventListeners();
}


HTMLSlotElement* ShadowRoot::findAssignedSlot(const Node& node)
{
    ASSERT(node.parentNode() == host());
    return m_slotAssignment ? m_slotAssignment->findAssignedSlot(node) : nullptr;
}

void ShadowRoot::renameSlotElement(HTMLSlotElement& slot, const AtomString& oldName, const AtomString& newName)
{
    ASSERT(m_slotAssignment);
    return m_slotAssignment->renameSlotElement(slot, oldName, newName, *this);
}

void ShadowRoot::addSlotElementByName(const AtomString& name, HTMLSlotElement& slot)
{
    ASSERT(&slot.rootNode() == this);
    if (!m_slotAssignment) {
        if (m_slotAssignmentMode == SlotAssignmentMode::Named)
            m_slotAssignment = makeUnique<NamedSlotAssignment>();
        else
            m_slotAssignment = makeUnique<ManualSlotAssignment>();
    }

    return m_slotAssignment->addSlotElementByName(name, slot, *this);
}

void ShadowRoot::removeSlotElementByName(const AtomString& name, HTMLSlotElement& slot, ContainerNode& oldParentOfRemovedTree)
{
    ASSERT(m_slotAssignment);
    return m_slotAssignment->removeSlotElementByName(name, slot, &oldParentOfRemovedTree, *this);
}

void ShadowRoot::slotManualAssignmentDidChange(HTMLSlotElement& slot, Vector<WeakPtr<Node, WeakPtrImplWithEventTargetData>>& previous, Vector<WeakPtr<Node, WeakPtrImplWithEventTargetData>>& current)
{
    ASSERT(m_slotAssignment);
    m_slotAssignment->slotManualAssignmentDidChange(slot, previous, current, *this);
}

void ShadowRoot::didRemoveManuallyAssignedNode(HTMLSlotElement& slot, const Node& node)
{
    ASSERT(m_slotAssignment);
    m_slotAssignment->didRemoveManuallyAssignedNode(slot, node, *this);
}

void ShadowRoot::slotFallbackDidChange(HTMLSlotElement& slot)
{
    ASSERT(&slot.rootNode() == this);
    return m_slotAssignment->slotFallbackDidChange(slot, *this);
}

const Vector<WeakPtr<Node, WeakPtrImplWithEventTargetData>>* ShadowRoot::assignedNodesForSlot(const HTMLSlotElement& slot)
{
    return m_slotAssignment ? m_slotAssignment->assignedNodesForSlot(slot, *this) : nullptr;
}

static std::optional<std::pair<AtomString, AtomString>> parsePartMapping(StringView mappingString)
{
    const auto end = mappingString.length();

    auto skipWhitespace = [&](auto position) {
        while (position < end && isASCIIWhitespace(mappingString[position]))
            ++position;
        return position;
    };

    auto collectValue = [&](auto position) {
        while (position < end && (!isASCIIWhitespace(mappingString[position]) && mappingString[position] != ':'))
            ++position;
        return position;
    };

    size_t begin = 0;
    begin = skipWhitespace(begin);

    auto firstPartEnd = collectValue(begin);
    if (firstPartEnd == begin)
        return { };

    auto firstPart = mappingString.substring(begin, firstPartEnd - begin).toAtomString();

    begin = skipWhitespace(firstPartEnd);
    if (begin == end)
        return std::make_pair(firstPart, firstPart);

    if (mappingString[begin] != ':')
        return { };

    begin = skipWhitespace(begin + 1);

    auto secondPartEnd = collectValue(begin);
    if (secondPartEnd == begin)
        return { };

    auto secondPart = mappingString.substring(begin, secondPartEnd - begin).toAtomString();

    begin = skipWhitespace(secondPartEnd);
    if (begin != end)
        return { };

    return std::make_pair(firstPart, secondPart);
}

static ShadowRoot::PartMappings parsePartMappingsList(StringView mappingsListString)
{
    ShadowRoot::PartMappings mappings;

    const auto end = mappingsListString.length();

    size_t begin = 0;
    while (begin < end) {
        size_t mappingEnd = begin;
        while (mappingEnd < end && mappingsListString[mappingEnd] != ',')
            ++mappingEnd;

        auto result = parsePartMapping(mappingsListString.substring(begin, mappingEnd - begin));
        if (result)
            mappings.add(result->first, Vector<AtomString, 1>()).iterator->value.append(result->second);

        if (mappingEnd == end)
            break;

        begin = mappingEnd + 1;
    }

    return mappings;
}

const ShadowRoot::PartMappings& ShadowRoot::partMappings() const
{
    if (!m_partMappings) {
        auto exportpartsValue = host()->attributeWithoutSynchronization(HTMLNames::exportpartsAttr);
        m_partMappings = parsePartMappingsList(exportpartsValue);
    }

    return *m_partMappings;
}

void ShadowRoot::invalidatePartMappings()
{
    m_partMappings = { };
}

Vector<Ref<ShadowRoot>> assignedShadowRootsIfSlotted(const Node& node)
{
    Vector<Ref<ShadowRoot>> result;
    for (auto* slot = node.assignedSlot(); slot; slot = slot->assignedSlot()) {
        ASSERT(slot->containingShadowRoot());
        result.append(*slot->containingShadowRoot());
    }
    return result;
}

#if ENABLE(PICTURE_IN_PICTURE_API)
Element* ShadowRoot::pictureInPictureElement() const
{
    notImplemented();
    return nullptr;
}
#endif

Vector<RefPtr<WebAnimation>> ShadowRoot::getAnimations()
{
    return document().matchingAnimations([&] (Element& target) -> bool {
        return target.containingShadowRoot() == this;
    });
}

void ShadowRoot::setReferenceTarget(const AtomString& referenceTarget)
{
    if (!document().settings().shadowRootReferenceTargetEnabled())
        return;

    if (m_referenceTarget == referenceTarget)
        return;

    m_referenceTarget = referenceTarget;

    if (CheckedPtr cache = document().existingAXObjectCache())
        cache->handleReferenceTargetChanged();
}

} // namespace WebCore
