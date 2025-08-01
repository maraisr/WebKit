/*
 * Copyright (C) 1997 Martin Jones (mjones@kde.org)
 *           (C) 1997 Torben Weis (weis@kde.org)
 *           (C) 1998 Waldo Bastian (bastian@kde.org)
 *           (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003-2025 Apple Inc. All rights reserved.\
 * Copyright (C) 2016 Google Inc. All rights reserved.
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

#pragma once

#include "CSSPropertyNames.h"
#include "CollapsedBorderValue.h"
#include "RenderBlock.h"
#include <memory>
#include <wtf/HashMap.h>
#include <wtf/Vector.h>

namespace WebCore {

class RenderTableCol;
class RenderTableCaption;
class RenderTableCell;
class RenderTableSection;
class TableLayout;

enum SkipEmptySectionsValue { DoNotSkipEmptySections, SkipEmptySections };
enum class TableIntrinsics : uint8_t { ForLayout, ForKeyword };

class RenderTable : public RenderBlock {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(RenderTable);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(RenderTable);
public:
    RenderTable(Type, Element&, RenderStyle&&);
    RenderTable(Type, Document&, RenderStyle&&);
    virtual ~RenderTable();

    // Per CSS 3 writing-mode: "The first and second values of the 'border-spacing' property represent spacing between columns
    // and rows respectively, not necessarily the horizontal and vertical spacing respectively".
    LayoutUnit hBorderSpacing() const { return m_hSpacing; }
    LayoutUnit vBorderSpacing() const { return m_vSpacing; }
    
    bool collapseBorders() const { return style().borderCollapse() == BorderCollapse::Collapse; }

    LayoutUnit borderStart() const final { return m_borderStart; }
    LayoutUnit borderEnd() const final { return m_borderEnd; }
    LayoutUnit borderBefore() const final;
    LayoutUnit borderAfter() const final;

    RectEdges<LayoutUnit> borderWidths() const final;
    inline LayoutUnit borderLeft() const final;
    inline LayoutUnit borderRight() const final;
    inline LayoutUnit borderTop() const final;
    inline LayoutUnit borderBottom() const final;

    Color bgColor() const { return checkedStyle()->visitedDependentColorWithColorFilter(CSSPropertyBackgroundColor); }

    LayoutUnit outerBorderBefore() const;
    LayoutUnit outerBorderAfter() const;
    LayoutUnit outerBorderStart() const;
    LayoutUnit outerBorderEnd() const;

    inline LayoutUnit outerBorderLeft() const;
    inline LayoutUnit outerBorderRight() const;
    inline LayoutUnit outerBorderTop() const;
    inline LayoutUnit outerBorderBottom() const;

    LayoutUnit calcBorderStart() const;
    LayoutUnit calcBorderEnd() const;
    void recalcBordersInRowDirection();

    void forceSectionsRecalc()
    {
        setNeedsSectionRecalc();
        recalcSections();
    }

    struct ColumnStruct {
        unsigned span { 1 };
    };
    const Vector<ColumnStruct>& columns() const { return m_columns; }
    const Vector<LayoutUnit>& columnPositions() const { return m_columnPos; }
    void setColumnPosition(unsigned index, LayoutUnit position)
    {
        // Note that if our horizontal border-spacing changed, our position will change but not
        // our column's width. In practice, horizontal border-spacing won't change often.
        m_columnLogicalWidthChanged |= m_columnPos[index] != position;
        m_columnPos[index] = position;
    }

    RenderTableSection* header() const;
    RenderTableSection* footer() const;
    RenderTableSection* firstBody() const;

    // This function returns 0 if the table has no section.
    RenderTableSection* topSection() const;
    RenderTableSection* bottomSection() const;

    // This function returns 0 if the table has no non-empty sections.
    RenderTableSection* topNonEmptySection() const;
    RenderTableSection* bottomNonEmptySection() const;

    unsigned lastColumnIndex() const { return numEffCols() - 1; }

    void splitColumn(unsigned position, unsigned firstSpan);
    void appendColumn(unsigned span);
    unsigned numEffCols() const { return m_columns.size(); }
    unsigned spanOfEffCol(unsigned effCol) const { return m_columns[effCol].span; }
    
    unsigned colToEffCol(unsigned column) const
    {
        if (!m_hasCellColspanThatDeterminesTableWidth)
            return column;

        unsigned effColumn = 0;
        unsigned numColumns = numEffCols();
        for (unsigned c = 0; effColumn < numColumns && c + m_columns[effColumn].span - 1 < column; ++effColumn)
            c += m_columns[effColumn].span;
        return effColumn;
    }
    
    unsigned effColToCol(unsigned effCol) const
    {
        if (!m_hasCellColspanThatDeterminesTableWidth)
            return effCol;

        unsigned c = 0;
        for (unsigned i = 0; i < effCol; i++)
            c += m_columns[i].span;
        return c;
    }

    LayoutUnit borderSpacingInRowDirection() const
    {
        if (unsigned effectiveColumnCount = numEffCols())
            return (effectiveColumnCount + 1) * hBorderSpacing();

        return 0;
    }

    inline LayoutUnit bordersPaddingAndSpacingInRowDirection() const;

    // Return the first column or column-group.
    RenderTableCol* firstColumn() const;

    RenderTableCol* colElement(unsigned col, bool* startEdge = 0, bool* endEdge = 0) const
    {
        // The common case is to not have columns, make that case fast.
        if (!m_hasColElements)
            return 0;
        return slowColElement(col, startEdge, endEdge);
    }

    bool needsSectionRecalc() const { return m_needsSectionRecalc; }
    void setNeedsSectionRecalc();

    RenderTableSection* sectionAbove(const RenderTableSection*, SkipEmptySectionsValue = DoNotSkipEmptySections) const;
    RenderTableSection* sectionBelow(const RenderTableSection*, SkipEmptySectionsValue = DoNotSkipEmptySections) const;

    RenderTableCell* cellAbove(const RenderTableCell*) const;
    RenderTableCell* cellBelow(const RenderTableCell*) const;
    RenderTableCell* cellBefore(const RenderTableCell*) const;
    RenderTableCell* cellAfter(const RenderTableCell*) const;
 
    typedef Vector<CollapsedBorderValue> CollapsedBorderValues;
    bool collapsedBordersAreValid() const { return m_collapsedBordersValid; }
    void invalidateCollapsedBorders(RenderTableCell* cellWithStyleChange = nullptr);
    void invalidateCollapsedBordersAfterStyleChangeIfNeeded(const RenderStyle& oldStyle, const RenderStyle& newStyle, RenderTableCell* cellWithStyleChange = nullptr);
    void collapsedEmptyBorderIsPresent() { m_collapsedEmptyBorderIsPresent = true; }
    const CollapsedBorderValue* currentBorderValue() const { return m_currentBorder; }
    
    bool hasSections() const { return m_head || m_foot || m_firstBody; }

    void recalcSectionsIfNeeded() const
    {
        if (m_needsSectionRecalc)
            recalcSections();
    }

    void addCaption(RenderTableCaption&);
    void removeCaption(RenderTableCaption&);
    void addColumn(const RenderTableCol*);
    void invalidateColumns();

    LayoutUnit offsetTopForColumn(const RenderTableCol&) const;
    LayoutUnit offsetLeftForColumn(const RenderTableCol&) const;
    LayoutUnit offsetWidthForColumn(const RenderTableCol&) const;
    LayoutUnit offsetHeightForColumn(const RenderTableCol&) const;
    
    void markForPaginationRelayoutIfNeeded() final;

    void willInsertTableColumn(RenderTableCol& child, RenderObject* beforeChild);
    void willInsertTableSection(RenderTableSection& child, RenderObject* beforeChild);

    LayoutUnit sumCaptionsLogicalHeight() const;

    // Whether a table has opaque foreground depends on many factors, e.g. border spacing, missing cells, etc.
    // For simplicity, just conservatively assume foreground of all tables are not opaque.
    bool foregroundIsKnownToBeOpaqueInRect(const LayoutRect&, unsigned) const override { return false; }

protected:
    void styleDidChange(StyleDifference, const RenderStyle* oldStyle) final;
    void simplifiedNormalFlowLayout() final;

    ASCIILiteral renderName() const override { return "RenderTable"_s; }

    void paint(PaintInfo&, const LayoutPoint&) final;
    void paintObject(PaintInfo&, const LayoutPoint&) final;
    void paintBoxDecorations(PaintInfo&, const LayoutPoint&) final;
    void paintMask(PaintInfo&, const LayoutPoint&) final;
    void layout() final;
    void computeIntrinsicLogicalWidths(LayoutUnit& minWidth, LayoutUnit& maxWidth, TableIntrinsics) const;
    void computeIntrinsicLogicalWidths(LayoutUnit& minWidth, LayoutUnit& maxWidth) const final;
    void computeIntrinsicKeywordLogicalWidths(LayoutUnit& minWidth, LayoutUnit& maxWidth) const final;
    void computePreferredLogicalWidths() override;
    bool nodeAtPoint(const HitTestRequest&, HitTestResult&, const HitTestLocation& locationInContainer, const LayoutPoint& accumulatedOffset, HitTestAction) override;

    std::optional<LayoutUnit> firstLineBaseline() const override;
    std::optional<LayoutUnit> lastLineBaseline() const override;

    RenderTableCol* slowColElement(unsigned col, bool* startEdge, bool* endEdge) const;

    void updateColumnCache() const;
    void invalidateCachedColumns();

    void invalidateCachedColumnOffsets();
    
    void updateLogicalWidth() final;

    template<typename SizeType> LayoutUnit convertStyleLogicalWidthToComputedWidth(const SizeType& styleLogicalWidth, LayoutUnit availableWidth);
    template<typename SizeType> LayoutUnit convertStyleLogicalHeightToComputedHeight(const SizeType& styleLogicalHeight);

    LayoutRect overflowClipRect(const LayoutPoint& location, OverlayScrollbarSizeRelevancy = OverlayScrollbarSizeRelevancy::IgnoreOverlayScrollbarSize, PaintPhase = PaintPhase::BlockBackground) const final;
    LayoutRect overflowClipRectForChildLayers(const LayoutPoint& location, OverlayScrollbarSizeRelevancy relevancy) const override { return RenderBox::overflowClipRect(location, relevancy); }

    void addOverflowFromChildren() final;

    void adjustBorderBoxRectForPainting(LayoutRect&) override;

    void recalcCollapsedBorders();
    void recalcSections() const;
    enum class BottomCaptionLayoutPhase : bool { No, Yes };
    void layoutCaptions(BottomCaptionLayoutPhase = BottomCaptionLayoutPhase::No);
    void layoutCaption(RenderTableCaption&);

    void distributeExtraLogicalHeight(LayoutUnit extraLogicalHeight);

    mutable Vector<LayoutUnit> m_columnPos;
    mutable Vector<ColumnStruct> m_columns;
    mutable Vector<SingleThreadWeakPtr<RenderTableCaption>> m_captions;
    mutable Vector<SingleThreadWeakPtr<RenderTableCol>> m_columnRenderers;

    unsigned effectiveIndexOfColumn(const RenderTableCol&) const;
    using EffectiveColumnIndexMap = HashMap<SingleThreadWeakRef<const RenderTableCol>, unsigned>;
    mutable EffectiveColumnIndexMap m_effectiveColumnIndexMap;

    mutable SingleThreadWeakPtr<RenderTableSection> m_head;
    mutable SingleThreadWeakPtr<RenderTableSection> m_foot;
    mutable SingleThreadWeakPtr<RenderTableSection> m_firstBody;

    std::unique_ptr<TableLayout> m_tableLayout;

    CollapsedBorderValues m_collapsedBorders;
    const CollapsedBorderValue* m_currentBorder;
    bool m_collapsedBordersValid : 1;
    bool m_collapsedEmptyBorderIsPresent : 1;

    mutable bool m_hasColElements : 1;
    mutable bool m_needsSectionRecalc : 1;

    bool m_columnLogicalWidthChanged : 1;
    mutable bool m_columnRenderersValid: 1;
    mutable bool m_hasCellColspanThatDeterminesTableWidth : 1;

    bool hasCellColspanThatDeterminesTableWidth() const
    {
        for (unsigned c = 0; c < numEffCols(); c++) {
            if (m_columns[c].span > 1)
                return true;
        }
        return false;
    }

    bool shouldResetLogicalHeightBeforeLayout() const override { return true; }

    LayoutUnit m_hSpacing;
    LayoutUnit m_vSpacing;
    LayoutUnit m_borderStart;
    LayoutUnit m_borderEnd;
    mutable LayoutUnit m_columnOffsetTop;
    mutable LayoutUnit m_columnOffsetHeight;
    unsigned m_recursiveSectionMovedWithPaginationLevel { 0 };
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_RENDER_OBJECT(RenderTable, isRenderTable())
