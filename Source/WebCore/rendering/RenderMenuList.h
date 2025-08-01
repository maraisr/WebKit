/*
 * This file is part of the select element renderer in WebCore.
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011, 2015 Apple Inc. All rights reserved.
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

#include "LayoutRect.h"
#include "PopupMenu.h"
#include "PopupMenuClient.h"
#include "RenderFlexibleBox.h"

#if PLATFORM(COCOA)
#define POPUP_MENU_PULLS_DOWN 0
#else
#define POPUP_MENU_PULLS_DOWN 1
#endif

namespace WebCore {

class HTMLSelectElement;
class RenderText;

class RenderMenuList final : public RenderFlexibleBox, private PopupMenuClient {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(RenderMenuList);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(RenderMenuList);
public:
    RenderMenuList(HTMLSelectElement&, RenderStyle&&);
    virtual ~RenderMenuList();

    HTMLSelectElement& selectElement() const;

    // CheckedPtr interface.
    uint32_t checkedPtrCount() const final { return RenderFlexibleBox::checkedPtrCount(); }
    uint32_t checkedPtrCountWithoutThreadCheck() const final { return RenderFlexibleBox::checkedPtrCountWithoutThreadCheck(); }
    void incrementCheckedPtrCount() const final { RenderFlexibleBox::incrementCheckedPtrCount(); }
    void decrementCheckedPtrCount() const final { RenderFlexibleBox::decrementCheckedPtrCount(); }

#if !PLATFORM(IOS_FAMILY)
    bool popupIsVisible() const { return m_popupIsVisible; }
#endif
    void showPopup();
    void hidePopup();

    void setOptionsChanged(bool changed) { m_needsOptionsWidthUpdate = changed; }

    void didSetSelectedIndex(int listIndex);

    String text() const;

#if PLATFORM(IOS_FAMILY)
    void layout() override;
#endif

    RenderBlock* innerRenderer() const { return m_innerBlock.get(); }
    void setInnerRenderer(RenderBlock&);

    void didAttachChild(RenderObject& child, RenderObject* beforeChild);

private:
    void willBeDestroyed() override;

    void element() const = delete;

    bool createsAnonymousWrapper() const override { return true; }

    void updateFromElement() override;

    LayoutRect controlClipRect(const LayoutPoint&) const override;
    bool hasControlClip() const override { return true; }
    bool canHaveGeneratedChildren() const override { return false; }

    ASCIILiteral renderName() const override { return "RenderMenuList"_s; }

    void computeIntrinsicLogicalWidths(LayoutUnit& minLogicalWidth, LayoutUnit& maxLogicalWidth) const override;
    void computePreferredLogicalWidths() override;

    void styleDidChange(StyleDifference, const RenderStyle* oldStyle) override;

    // PopupMenuClient methods
    void valueChanged(unsigned listIndex, bool fireOnChange = true) override;
    void selectionChanged(unsigned, bool) override { }
    void selectionCleared() override { }
    String itemText(unsigned listIndex) const override;
    String itemLabel(unsigned listIndex) const override;
    String itemIcon(unsigned listIndex) const override;
    String itemToolTip(unsigned listIndex) const override;
    String itemAccessibilityText(unsigned listIndex) const override;
    bool itemIsEnabled(unsigned listIndex) const override;
    PopupMenuStyle itemStyle(unsigned listIndex) const override;
    PopupMenuStyle menuStyle() const override;
    int clientInsetLeft() const override;
    int clientInsetRight() const override;
    LayoutUnit clientPaddingLeft() const override;
    LayoutUnit clientPaddingRight() const override;
    int listSize() const override;
    int selectedIndex() const override;
    void popupDidHide() override;
    bool itemIsSeparator(unsigned listIndex) const override;
    bool itemIsLabel(unsigned listIndex) const override;
    bool itemIsSelected(unsigned listIndex) const override;
    bool shouldPopOver() const override { return !POPUP_MENU_PULLS_DOWN; }
    void setTextFromItem(unsigned listIndex) override;
    void listBoxSelectItem(int listIndex, bool allowMultiplySelections, bool shift, bool fireOnChangeNow = true) override;
    bool multiple() const override;
    FontSelector* fontSelector() const override;
    HostWindow* hostWindow() const override;
    Ref<Scrollbar> createScrollbar(ScrollableArea&, ScrollbarOrientation, ScrollbarWidth) override;

    bool hasLineIfEmpty() const override { return true; }

    std::optional<LayoutUnit> firstLineBaseline() const override { return RenderBlock::firstLineBaseline(); }

    void getItemBackgroundColor(unsigned listIndex, Color&, bool& itemHasCustomBackgroundColor) const;

    void adjustInnerStyle();
    void setText(const String&);
    void setTextFromOption(int optionIndex);
    void updateOptionsWidth();

    void didUpdateActiveOption(int optionIndex);

    bool isFlexibleBoxImpl() const override { return true; }

    SingleThreadWeakPtr<RenderText> m_buttonText;
    SingleThreadWeakPtr<RenderBlock> m_innerBlock;

    bool m_needsOptionsWidthUpdate;
    int m_optionsWidth;

    std::optional<int> m_lastActiveIndex;

    std::unique_ptr<RenderStyle> m_optionStyle;

#if !PLATFORM(IOS_FAMILY)
    RefPtr<PopupMenu> m_popup;
    bool m_popupIsVisible;
#endif
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_RENDER_OBJECT(RenderMenuList, isRenderMenuList())
