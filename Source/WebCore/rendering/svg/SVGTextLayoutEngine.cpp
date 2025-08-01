/*
 * Copyright (C) Research In Motion Limited 2010-2012. All rights reserved.
 * Copyright (C) Apple 2023-2024. All rights reserved.
 * Copyright (C) Google 2014-2017. All rights reserved.
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
#include "SVGTextLayoutEngine.h"

#include "PathTraversalState.h"
#include "RenderElementInlines.h"
#include "RenderSVGTextPath.h"
#include "RenderStyleInlines.h"
#include "SVGElement.h"
#include "SVGGeometryElement.h"
#include "SVGInlineTextBoxInlines.h"
#include "SVGLengthContext.h"
#include "SVGTextContentElement.h"
#include "SVGTextLayoutEngineBaseline.h"
#include "SVGTextLayoutEngineSpacing.h"

// Set to a value > 0 to dump the text fragments
#define DUMP_SVG_TEXT_LAYOUT_FRAGMENTS 0

namespace WebCore {

SVGTextLayoutEngine::SVGTextLayoutEngine(Vector<SVGTextLayoutAttributes*>& layoutAttributes)
    : m_layoutAttributes(layoutAttributes)
{
    ASSERT(!m_layoutAttributes.isEmpty());
}

void SVGTextLayoutEngine::updateCharacterPositionIfNeeded(float& x, float& y)
{
    if (m_inPathLayout)
        return;

    // Replace characters x/y position, with the current text position plus any
    // relative adjustments, if it doesn't specify an absolute position itself.
    if (SVGTextLayoutAttributes::isEmptyValue(x))
        x = m_x + m_dx;

    if (SVGTextLayoutAttributes::isEmptyValue(y))
        y = m_y + m_dy;

    m_dx = 0;
    m_dy = 0;
}

void SVGTextLayoutEngine::updateCurrentTextPosition(float x, float y, float glyphAdvance)
{
    // Update current text position after processing the character.
    if (m_isVerticalText) {
        m_x = x;
        m_y = y + glyphAdvance;
    } else {
        m_x = x + glyphAdvance;
        m_y = y;
    }
}

void SVGTextLayoutEngine::updateRelativePositionAdjustmentsIfNeeded(float dx, float dy)
{
    // Update relative positioning information.
    if (SVGTextLayoutAttributes::isEmptyValue(dx) && SVGTextLayoutAttributes::isEmptyValue(dy))
        return;

    if (SVGTextLayoutAttributes::isEmptyValue(dx))
        dx = 0;
    if (SVGTextLayoutAttributes::isEmptyValue(dy))
        dy = 0;

    if (m_inPathLayout) {
        if (m_isVerticalText) {
            m_dx += dx;
            m_dy = dy;
        } else {
            m_dx = dx;
            m_dy += dy;
        }

        return;
    }

    m_dx = dx;
    m_dy = dy;
}

void SVGTextLayoutEngine::recordTextFragment(InlineIterator::SVGTextBoxIterator textBox, const Vector<SVGTextMetrics>& textMetricsValues)
{
    ASSERT(!m_currentTextFragment.length);
    ASSERT(m_visualMetricsListOffset > 0);

    // Figure out length of fragment.
    m_currentTextFragment.length = m_visualCharacterOffset - m_currentTextFragment.characterOffset;

    // Figure out fragment metrics.
    auto& lastCharacterMetrics = textMetricsValues.at(m_visualMetricsListOffset - 1);
    m_currentTextFragment.width = lastCharacterMetrics.width();
    m_currentTextFragment.height = lastCharacterMetrics.height();

    if (m_currentTextFragment.length > 1) {
        // SVGTextLayoutAttributesBuilder assures that the length of the range is equal to the sum of the individual lengths of the glyphs.
        float length = 0;
        if (m_isVerticalText) {
            for (unsigned i = m_currentTextFragment.metricsListOffset; i < m_visualMetricsListOffset; ++i)
                length += textMetricsValues.at(i).height();
            m_currentTextFragment.height = length;
        } else {
            for (unsigned i = m_currentTextFragment.metricsListOffset; i < m_visualMetricsListOffset; ++i)
                length += textMetricsValues.at(i).width();
            m_currentTextFragment.width = length;
        }
    }

    auto& fragments = m_fragmentMap.ensure(makeKey(*textBox), [&] {
        return Vector<SVGTextFragment> { };
    }).iterator->value;

    fragments.append(m_currentTextFragment);
    m_currentTextFragment = SVGTextFragment();
}

bool SVGTextLayoutEngine::parentDefinesTextLength(RenderObject* parent) const
{
    RenderObject* currentParent = parent;
    while (currentParent) {
        if (RefPtr textContentElement = SVGTextContentElement::elementFromRenderer(currentParent)) {
            SVGLengthContext lengthContext(textContentElement.get());
            if (textContentElement->lengthAdjust() == SVGLengthAdjustSpacing && textContentElement->specifiedTextLength().value(lengthContext) > 0)
                return true;
        }

        if (currentParent->isRenderSVGText())
            return false;

        currentParent = currentParent->parent();
    }

    ASSERT_NOT_REACHED();
    return false;
}

void SVGTextLayoutEngine::beginTextPathLayout(const RenderSVGTextPath& textPath, SVGTextLayoutEngine& lineLayout)
{
    m_inPathLayout = true;

    m_textPath = textPath.layoutPath();
    if (m_textPath.isEmpty())
        return;

    const auto& startOffset = textPath.startOffset();
    m_textPathLength = m_textPath.length();
    
    if (textPath.startOffset().lengthType() == SVGLengthType::Percentage)
        m_textPathStartOffset = startOffset.valueAsPercentage() * m_textPathLength;
    else {
        m_textPathStartOffset = startOffset.valueInSpecifiedUnits();
        if (RefPtr targetElement = textPath.targetElement()) {
            // FIXME: A value of zero is valid. Need to differentiate this case from being unspecified.
            if (float pathLength = targetElement->pathLength())
                m_textPathStartOffset *= m_textPathLength / pathLength;
        }
    }

    lineLayout.m_chunkLayoutBuilder.buildTextChunks(lineLayout.m_lineLayoutBoxes, lineLayout.m_lineLayoutChunkStarts, lineLayout.m_fragmentMap);

    // Handle text-anchor as additional start offset for text paths.
    m_textPathStartOffset += lineLayout.m_chunkLayoutBuilder.totalAnchorShift();
    m_textPathCurrentOffset = m_textPathStartOffset;

    // Eventually handle textLength adjustments.
    RefPtr textContentElement = SVGTextContentElement::elementFromRenderer(&textPath);
    if (!textContentElement)
        return;

    SVGLengthContext lengthContext(textContentElement.get());
    float desiredTextLength = textContentElement->specifiedTextLength().value(lengthContext);
    if (!desiredTextLength)
        return;

    float totalLength = lineLayout.m_chunkLayoutBuilder.totalLength();
    unsigned totalCharacters = lineLayout.m_chunkLayoutBuilder.totalCharacters();

    if (textContentElement->lengthAdjust() == SVGLengthAdjustSpacing) {
        if (totalCharacters > 1)
            m_textPathSpacing = (desiredTextLength - totalLength) / (totalCharacters - 1);
    } else
        m_textPathScaling = desiredTextLength / totalLength;
}

void SVGTextLayoutEngine::endTextPathLayout()
{
    m_inPathLayout = false;
    m_textPath = Path();
    m_textPathLength = 0;
    m_textPathStartOffset = 0;
    m_textPathCurrentOffset = 0;
    m_textPathSpacing = 0;
    m_textPathScaling = 1;
}

void SVGTextLayoutEngine::layoutInlineTextBox(InlineIterator::SVGTextBoxIterator textBox)
{
    auto& text = textBox->renderer();
    ASSERT(text.parent());
    ASSERT(text.parent()->element());
    ASSERT(text.parent()->element()->isSVGElement());

    const RenderStyle& style = text.style();

    m_isVerticalText = style.writingMode().isVertical();
    layoutTextOnLineOrPath(textBox, text, style);

    if (m_inPathLayout) {
        m_pathLayoutBoxes.append(textBox);
        return;
    }

    m_lineLayoutBoxes.append(textBox);
}

#if DUMP_SVG_TEXT_LAYOUT_FRAGMENTS > 0
static inline void dumpTextBoxes(Vector<InlineIterator::SVGTextBoxIterator>& boxes)
{
    auto boxCount = boxes.size();
    fprintf(stderr, "Dumping all text fragments in text sub tree, %ld boxes\n", boxCount);

    for (unsigned boxPosition = 0; boxPosition < boxCount; ++boxPosition) {
        auto textBox = boxes.at(boxPosition);
        Vector<SVGTextFragment>& fragments = textBox->textFragments();
        fprintf(stderr, "-> Box %d: Dumping text fragments for SVGInlineTextBox, textBox=%p, textRenderer=%p\n", boxPosition, textBox, &textBox->renderer());
        fprintf(stderr, "        textBox properties, start=%d, len=%d, box direction=%d\n", textBox->start(), textBox->len(), (int)textBox->direction());
        fprintf(stderr, "   textRenderer properties, textLength=%d\n", textBox->renderer().text().length());

        const auto* characters = textBox->renderer().text().characters<char16_t>();

        unsigned fragmentCount = fragments.size();
        for (unsigned i = 0; i < fragmentCount; ++i) {
            SVGTextFragment& fragment = fragments.at(i);
            String fragmentString(characters + fragment.characterOffset, fragment.length);
            fprintf(stderr, "    -> Fragment %d, x=%.2f, y=%.2f, width=%.2f, height=%.2f, characterOffset=%d, length=%d, characters='%s'\n"
                          , i, fragment.x, fragment.y, fragment.width, fragment.height, fragment.characterOffset, fragment.length, fragmentString.utf8().data());
        }
    }
}
#endif

void SVGTextLayoutEngine::finalizeTransformMatrices(Vector<InlineIterator::SVGTextBoxIterator>& textBoxes)
{
    if (textBoxes.isEmpty())
        return;

    for (auto textBox : textBoxes) {
        auto textBoxTransformation = m_chunkLayoutBuilder.transformationForTextBox(textBox);
        if (textBoxTransformation.isIdentity())
            continue;

        auto it = m_fragmentMap.find(makeKey(*textBox));
        if (it != m_fragmentMap.end()) {
            for (auto& fragment : it->value) {
                ASSERT(fragment.lengthAdjustTransform.isIdentity());
                fragment.lengthAdjustTransform = textBoxTransformation;
            }
        }
    }

    textBoxes.clear();
}

SVGTextFragmentMap SVGTextLayoutEngine::finishLayout()
{
    // After all text fragments are stored in their correpsonding SVGInlineTextBoxes, we can layout individual text chunks.
    // Chunk layouting is only performed for line layout boxes, not for path layout, where it has already been done.
    m_chunkLayoutBuilder.layoutTextChunks(m_lineLayoutBoxes, m_lineLayoutChunkStarts, m_fragmentMap);

    // Finalize transform matrices, after the chunk layout corrections have been applied, and all fragment x/y positions are finalized.
    if (!m_lineLayoutBoxes.isEmpty()) {
#if DUMP_SVG_TEXT_LAYOUT_FRAGMENTS > 0
        fprintf(stderr, "Line layout: ");
        dumpTextBoxes(m_lineLayoutBoxes);
#endif

        finalizeTransformMatrices(m_lineLayoutBoxes);
    }

    if (!m_pathLayoutBoxes.isEmpty()) {
#if DUMP_SVG_TEXT_LAYOUT_FRAGMENTS > 0
        fprintf(stderr, "Path layout: ");
        dumpTextBoxes(m_pathLayoutBoxes);
#endif

        finalizeTransformMatrices(m_pathLayoutBoxes);
    }

    return WTFMove(m_fragmentMap);
}

bool SVGTextLayoutEngine::currentLogicalCharacterAttributes(SVGTextLayoutAttributes*& logicalAttributes)
{
    if (m_layoutAttributesPosition == m_layoutAttributes.size())
        return false;

    logicalAttributes = m_layoutAttributes[m_layoutAttributesPosition];
    ASSERT(logicalAttributes);

    if (m_logicalCharacterOffset != logicalAttributes->context().text().length())
        return true;

    ++m_layoutAttributesPosition;
    if (m_layoutAttributesPosition == m_layoutAttributes.size())
        return false;

    logicalAttributes = m_layoutAttributes[m_layoutAttributesPosition];
    m_logicalMetricsListOffset = 0;
    m_logicalCharacterOffset = 0;
    return true;
}

bool SVGTextLayoutEngine::currentLogicalCharacterMetrics(SVGTextLayoutAttributes*& logicalAttributes, SVGTextMetrics& logicalMetrics)
{
    Vector<SVGTextMetrics>* textMetricsValues = &logicalAttributes->textMetricsValues();
    unsigned textMetricsSize = textMetricsValues->size();
    while (true) {
        if (m_logicalMetricsListOffset == textMetricsSize) {
            if (!currentLogicalCharacterAttributes(logicalAttributes))
                return false;

            textMetricsValues = &logicalAttributes->textMetricsValues();
            textMetricsSize = textMetricsValues->size();
            continue;
        }

        ASSERT(textMetricsSize);
        ASSERT_WITH_SECURITY_IMPLICATION(m_logicalMetricsListOffset < textMetricsSize);
        logicalMetrics = textMetricsValues->at(m_logicalMetricsListOffset);
        if (logicalMetrics.isEmpty() || (!logicalMetrics.width() && !logicalMetrics.height())) {
            advanceToNextLogicalCharacter(logicalMetrics);
            continue;
        }

        // Stop if we found the next valid logical text metrics object.
        return true;
    }

    ASSERT_NOT_REACHED();
    return true;
}

bool SVGTextLayoutEngine::currentVisualCharacterMetrics(const InlineIterator::SVGTextBox& textBox, const Vector<SVGTextMetrics>& visualMetricsValues, SVGTextMetrics& visualMetrics)
{
    ASSERT(!visualMetricsValues.isEmpty());
    unsigned textMetricsSize = visualMetricsValues.size();
    unsigned boxStart = textBox.start();
    unsigned boxLength = textBox.length();

    while (m_visualMetricsListOffset < textMetricsSize) {
        // Advance to text box start location.
        if (m_visualCharacterOffset < boxStart) {
            advanceToNextVisualCharacter(visualMetricsValues[m_visualMetricsListOffset]);
            continue;
        }

        // Stop if we've finished processing this text box.
        if (m_visualCharacterOffset >= boxStart + boxLength)
            return false;

        visualMetrics = visualMetricsValues[m_visualMetricsListOffset];
        return true;
    }

    return false;
}

void SVGTextLayoutEngine::advanceToNextLogicalCharacter(const SVGTextMetrics& logicalMetrics)
{
    ++m_logicalMetricsListOffset;
    m_logicalCharacterOffset += logicalMetrics.length();
}

void SVGTextLayoutEngine::advanceToNextVisualCharacter(const SVGTextMetrics& visualMetrics)
{
    ++m_visualMetricsListOffset;
    m_visualCharacterOffset += visualMetrics.length();
}

void SVGTextLayoutEngine::layoutTextOnLineOrPath(InlineIterator::SVGTextBoxIterator textBox, const RenderSVGInlineText& text, const RenderStyle& style)
{
    if (m_inPathLayout && m_textPath.isEmpty())
        return;

    RenderElement* textParent = text.parent();
    ASSERT(textParent);
    RefPtr lengthContext = downcast<SVGElement>(textParent->element());
    
    bool definesTextLength = parentDefinesTextLength(textParent);

    const SVGRenderStyle& svgStyle = style.svgStyle();

    m_visualMetricsListOffset = 0;
    m_visualCharacterOffset = 0;

    auto& visualMetricsValues = text.layoutAttributes()->textMetricsValues();
    ASSERT(!visualMetricsValues.isEmpty());

    auto upconvertedCharacters = StringView(text.text()).upconvertedCharacters();
    auto characters = upconvertedCharacters.span();
    const FontCascade& font = style.fontCascade();

    SVGTextLayoutEngineSpacing spacingLayout(font);
    SVGTextLayoutEngineBaseline baselineLayout(font);

    bool didStartTextFragment = false;
    bool applySpacingToNextCharacter = false;

    float lastAngle = 0;
    float baselineShift = baselineLayout.calculateBaselineShift(svgStyle);
    baselineShift -= baselineLayout.calculateAlignmentBaselineShift(m_isVerticalText, text);

    // Main layout algorithm.
    while (true) {
        // Find the start of the current text box in this list, respecting ligatures.
        SVGTextMetrics visualMetrics(SVGTextMetrics::SkippedSpaceMetrics);
        if (!currentVisualCharacterMetrics(*textBox, visualMetricsValues, visualMetrics))
            break;

        if (visualMetrics.isEmpty()) {
            advanceToNextVisualCharacter(visualMetrics);
            continue;
        }

        SVGTextLayoutAttributes* logicalAttributes = 0;
        if (!currentLogicalCharacterAttributes(logicalAttributes))
            break;

        ASSERT(logicalAttributes);
        SVGTextMetrics logicalMetrics(SVGTextMetrics::SkippedSpaceMetrics);
        if (!currentLogicalCharacterMetrics(logicalAttributes, logicalMetrics))
            break;

        SVGCharacterDataMap& characterDataMap = logicalAttributes->characterDataMap();
        SVGCharacterData data;
        SVGCharacterDataMap::iterator it = characterDataMap.find(m_logicalCharacterOffset + 1);
        if (it != characterDataMap.end())
            data = it->value;

        float x = data.x;
        float y = data.y;
        auto previousBoxOnLine = textBox->nextLineLeftwardOnLine();

        bool hasXOrY = !SVGTextLayoutAttributes::isEmptyValue(x) || !SVGTextLayoutAttributes::isEmptyValue(y);

        // If we start a new chunk following an chunk that had a textLength set, use that
        // textLength to determine the chunk start position, instead of glyph advance values.
        auto moveToExpectedChunkStartPositionIfNeeded = [&]() {
            if (m_inPathLayout || !m_lastChunkHasTextLength || !previousBoxOnLine)
                return;

            if (m_isVerticalText) {
                if (!SVGTextLayoutAttributes::isEmptyValue(y))
                    return;
            } else {
                if (!SVGTextLayoutAttributes::isEmptyValue(x))
                    return;
            }

            RefPtr textContentElement = SVGTextContentElement::elementFromRenderer(&previousBoxOnLine->renderer());
            if (!textContentElement)
                return;

            SVGLengthContext lengthContext(textContentElement.get());
            auto specifiedTextLength = textContentElement->specifiedTextLength().value(lengthContext);

            if (m_lastChunkIsVerticalText)
                y = m_lastChunkStartPosition + specifiedTextLength;
            else
                x = m_lastChunkStartPosition + specifiedTextLength;
        };

        bool startsNewTextChunk = [&]() {
            // If we're at a position that could start a new text chunk, but doesn't for intrinsic reasons (no x/y information specified for the
            // current character), check further if there are other conditions met that enforce a new text chunk -- e.g. previous sibiling on the
            // same line specified 'textLength' (consider: <text><tspan textLength="100">AB</tspan> <tspan dy="1em">...
            // The space character is not allowed to be part of the 'AB' text chunk -- there is not explicit x/y given for the space character
            // but because of the textLength attribute, we have to keep the space in a separated chunk, and position it such that it renders
            // after the user-specified textLength.
            if (logicalAttributes->context().characterStartsNewTextChunk(m_logicalCharacterOffset))
                return true;

            // If we encounter an InlineTextBox that follows an InlineFlowBox with specified textLength,
            // and if the InlineTextBox content is not positioned by explicit x/y attributes, then we have
            // to correct the position of the InlineTextBox, to account for the textLength adjustments
            // that will be applied on chunk-level in the next SVG text layout phase. Failing to do so,
            // will lay out the remaining content at the nominal position, as if no textLength was given.
            if (m_lastChunkHasTextLength && previousBoxOnLine)
                return true;

            return false;
        }();

        // When we've advanced to the box start offset, determine using the original x/y values
        // whether this character starts a new text chunk before doing any further processing.
        if (m_visualCharacterOffset == textBox->start()) {
            moveToExpectedChunkStartPositionIfNeeded();
            if (startsNewTextChunk)
                m_lineLayoutChunkStarts.add(makeKey(*textBox));
        }

        float angle = SVGTextLayoutAttributes::isEmptyValue(data.rotate) ? 0 : data.rotate;

        // Calculate glyph orientation angle.
        const char16_t* currentCharacter = characters.subspan(m_visualCharacterOffset).data();
        float orientationAngle = baselineLayout.calculateGlyphOrientationAngle(m_isVerticalText, svgStyle, *currentCharacter);

        // Calculate glyph advance & x/y orientation shifts.
        float xOrientationShift = 0;
        float yOrientationShift = 0;
        float glyphAdvance = baselineLayout.calculateGlyphAdvanceAndOrientation(m_isVerticalText, visualMetrics, orientationAngle, xOrientationShift, yOrientationShift);

        // Assign current text position to x/y values, if needed.
        updateCharacterPositionIfNeeded(x, y);

        // Apply dx/dy value adjustments to current text position, if needed.
        updateRelativePositionAdjustmentsIfNeeded(data.dx, data.dy);

        // Calculate CSS 'letter-spacing' and 'word-spacing' for next character, if needed.
        float spacing = spacingLayout.calculateCSSSpacing(currentCharacter);

        float textPathOffset = 0;
        if (m_inPathLayout) {
            float scaledGlyphAdvance = glyphAdvance * m_textPathScaling;
            if (m_isVerticalText) {
                // If there's an absolute y position available, it marks the beginning of a new position along the path.
                if (!SVGTextLayoutAttributes::isEmptyValue(y))
                    m_textPathCurrentOffset = y + m_textPathStartOffset;

                m_textPathCurrentOffset += m_dy;
                m_dy = 0;

                // Apply dx/dy correction and setup translations that move to the glyph midpoint.
                xOrientationShift += m_dx + baselineShift;
                yOrientationShift -= scaledGlyphAdvance / 2;
            } else {
                // If there's an absolute x position available, it marks the beginning of a new position along the path.
                if (!SVGTextLayoutAttributes::isEmptyValue(x))
                    m_textPathCurrentOffset = x + m_textPathStartOffset;

                m_textPathCurrentOffset += m_dx;
                m_dx = 0;

                // Apply dx/dy correction and setup translations that move to the glyph midpoint.
                xOrientationShift -= scaledGlyphAdvance / 2;
                yOrientationShift += m_dy - baselineShift;
            }

            // Calculate current offset along path.
            textPathOffset = m_textPathCurrentOffset + scaledGlyphAdvance / 2;

            // Move to next character.
            m_textPathCurrentOffset += scaledGlyphAdvance + m_textPathSpacing + spacing * m_textPathScaling;

            // Skip character, if we're before the path.
            if (textPathOffset < 0) {
                advanceToNextLogicalCharacter(logicalMetrics);
                advanceToNextVisualCharacter(visualMetrics);
                continue;
            }

            // Stop processing, if the next character lies behind the path.
            if (textPathOffset > m_textPathLength)
                break;

            auto traversalState(m_textPath.traversalStateAtLength(textPathOffset));
            ASSERT(traversalState.success());

            FloatPoint point = traversalState.current();
            x = point.x();
            y = point.y();

            angle = traversalState.normalAngle();

            // For vertical text on path, the actual angle has to be rotated 90 degrees anti-clockwise, not the orientation angle!
            if (m_isVerticalText)
                angle -= 90;
        } else {
            // Apply all previously calculated shift values.
            if (m_isVerticalText)
                x += baselineShift;
            else
                y -= baselineShift;

            x += m_dx;
            y += m_dy;
        }

        // Remember the position / direction of the start position of the new text chunk.
        if (startsNewTextChunk) {
            m_lastChunkStartPosition = m_isVerticalText ? y : x;
            m_lastChunkIsVerticalText = m_isVerticalText;
            m_lastChunkHasTextLength = definesTextLength;
        }

        // Determine whether we have to start a new fragment.
        bool shouldStartNewFragment = hasXOrY || m_dx || m_dy || m_isVerticalText || m_inPathLayout || angle || angle != lastAngle
            || orientationAngle || applySpacingToNextCharacter || definesTextLength;

        // If we already started a fragment, close it now.
        if (didStartTextFragment && shouldStartNewFragment) {
            applySpacingToNextCharacter = false;
            recordTextFragment(textBox, visualMetricsValues);
        }

        // Eventually start a new fragment, if not yet done.
        if (!didStartTextFragment || shouldStartNewFragment) {
            ASSERT(!m_currentTextFragment.characterOffset);
            ASSERT(!m_currentTextFragment.length);

            didStartTextFragment = true;
            m_currentTextFragment.characterOffset = m_visualCharacterOffset;
            m_currentTextFragment.metricsListOffset = m_visualMetricsListOffset;
            m_currentTextFragment.x = x;
            m_currentTextFragment.y = y;

            // Build fragment transformation.
            if (angle)
                m_currentTextFragment.transform.rotate(angle);

            if (xOrientationShift || yOrientationShift)
                m_currentTextFragment.transform.translate(xOrientationShift, yOrientationShift);

            if (orientationAngle)
                m_currentTextFragment.transform.rotate(orientationAngle);

            m_currentTextFragment.isTextOnPath = m_inPathLayout && m_textPathScaling != 1;
            if (m_currentTextFragment.isTextOnPath) {
                if (m_isVerticalText)
                    m_currentTextFragment.lengthAdjustTransform.scaleNonUniform(1, m_textPathScaling);
                else
                    m_currentTextFragment.lengthAdjustTransform.scaleNonUniform(m_textPathScaling, 1);
            }
        }

        // Update current text position, after processing of the current character finished.
        if (m_inPathLayout)
            updateCurrentTextPosition(x, y, glyphAdvance);
        else {
            // Apply CSS 'letter-spacing' and 'word-spacing' to next character, if needed.
            if (spacing)
                applySpacingToNextCharacter = true;

            float xNew = x - m_dx;
            float yNew = y - m_dy;

            if (m_isVerticalText)
                xNew -= baselineShift;
            else
                yNew += baselineShift;

            updateCurrentTextPosition(xNew, yNew, glyphAdvance + spacing);
        }

        advanceToNextLogicalCharacter(logicalMetrics);
        advanceToNextVisualCharacter(visualMetrics);
        lastAngle = angle;
    }

    if (!didStartTextFragment)
        return;

    // Close last open fragment, if needed.
    recordTextFragment(textBox, visualMetricsValues);
}

}
