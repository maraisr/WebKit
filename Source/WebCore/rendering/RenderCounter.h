/*
 * Copyright (C) 2004 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2006-2022 Apple Inc. All rights reserved.
 * Copyright (C) 2015 Google Inc. All rights reserved.
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
 *
 */

#pragma once

#include "RenderText.h"
#include "StyleContent.h"

namespace WebCore {

class CSSCounterStyle;
class CounterNode;

class RenderCounter final : public RenderText {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(RenderCounter);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(RenderCounter);
public:
    RenderCounter(Document&, const Style::Content::Counter&);
    virtual ~RenderCounter();

    static void destroyCounterNodes(RenderElement&);
    static void destroyCounterNode(RenderElement&, const AtomString& identifier);
    static void rendererStyleChanged(RenderElement&, const RenderStyle* oldStyle, const RenderStyle& newStyle);

    void updateCounter();
    bool canBeSelectionLeaf() const final { return false; }

private:
    void willBeDestroyed() override;
    static void rendererStyleChangedSlowCase(RenderElement&, const RenderStyle* oldStyle, const RenderStyle& newStyle);
    
    ASCIILiteral renderName() const override;
    String originalText() const override;

    Ref<CSSCounterStyle> counterStyle() const;

    Style::Content::Counter m_counter;
    SingleThreadWeakPtr<CounterNode> m_counterNode;
    SingleThreadWeakPtr<RenderCounter> m_nextForSameCounter;
    friend class CounterNode;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_RENDER_OBJECT(RenderCounter, isRenderCounter())

#if ENABLE(TREE_DEBUGGING)
// Outside the WebCore namespace for ease of invocation from the debugger.
void showCounterRendererTree(const WebCore::RenderObject*, ASCIILiteral counterName = { });
#endif
