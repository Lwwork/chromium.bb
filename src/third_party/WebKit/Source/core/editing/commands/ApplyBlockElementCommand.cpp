/*
 * Copyright (C) 2006, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Google Inc. All rights reserved.
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

#include "core/editing/commands/ApplyBlockElementCommand.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/HTMLNames.h"
#include "core/dom/NodeComputedStyle.h"
#include "core/dom/Text.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/VisiblePosition.h"
#include "core/editing/VisibleUnits.h"
#include "core/html/HTMLBRElement.h"
#include "core/html/HTMLElement.h"
#include "core/style/ComputedStyle.h"

namespace blink {

using namespace HTMLNames;

ApplyBlockElementCommand::ApplyBlockElementCommand(Document& document, const QualifiedName& tagName, const AtomicString& inlineStyle)
    : CompositeEditCommand(document)
    , m_tagName(tagName)
    , m_inlineStyle(inlineStyle)
{
}

ApplyBlockElementCommand::ApplyBlockElementCommand(Document& document, const QualifiedName& tagName)
    : CompositeEditCommand(document)
    , m_tagName(tagName)
{
}

void ApplyBlockElementCommand::doApply(EditingState* editingState)
{
    VisiblePosition startOfSelection;
    VisiblePosition endOfSelection;
    ContainerNode* startScope = nullptr;
    ContainerNode* endScope = nullptr;
    int startIndex;
    int endIndex;

    if (!prepareForBlockCommand(startOfSelection, endOfSelection, startScope, endScope, startIndex, endIndex, false))
        return;

    formatSelection(startOfSelection, endOfSelection, editingState);

    if (editingState->isAborted())
        return;

    finishBlockCommand(startScope, endScope, startIndex, endIndex);
}

void ApplyBlockElementCommand::formatSelection(const VisiblePosition& startOfSelection, const VisiblePosition& endOfSelection, EditingState* editingState)
{
    // Special case empty unsplittable elements because there's nothing to split
    // and there's nothing to move.
    Position start = mostForwardCaretPosition(startOfSelection.deepEquivalent());
    if (isAtUnsplittableElement(start)) {
        HTMLElement* blockquote = createBlockElement();
        insertNodeAt(blockquote, start, editingState);
        if (editingState->isAborted())
            return;
        HTMLBRElement* placeholder = HTMLBRElement::create(document());
        appendNode(placeholder, blockquote, editingState);
        if (editingState->isAborted())
            return;
        setEndingSelection(VisibleSelection(Position::beforeNode(placeholder), TextAffinity::Downstream, endingSelection().isDirectional()));
        return;
    }

    HTMLElement* blockquoteForNextIndent = nullptr;
    VisiblePosition endOfCurrentParagraph = endOfParagraph(startOfSelection);
    VisiblePosition endOfLastParagraph = endOfParagraph(endOfSelection);
    VisiblePosition endAfterSelection = endOfParagraph(nextPositionOf(endOfLastParagraph));
    m_endOfLastParagraph = endOfLastParagraph.deepEquivalent();

    bool atEnd = false;
    Position end;
    while (endOfCurrentParagraph.deepEquivalent() != endAfterSelection.deepEquivalent() && !atEnd) {
        if (endOfCurrentParagraph.deepEquivalent() == m_endOfLastParagraph)
            atEnd = true;

        rangeForParagraphSplittingTextNodesIfNeeded(endOfCurrentParagraph, start, end);
        endOfCurrentParagraph = createVisiblePosition(end);

        Node* enclosingCell = enclosingNodeOfType(start, &isTableCell);
        VisiblePosition endOfNextParagraph = endOfNextParagrahSplittingTextNodesIfNeeded(endOfCurrentParagraph, start, end);

        formatRange(start, end, m_endOfLastParagraph, blockquoteForNextIndent, editingState);
        if (editingState->isAborted())
            return;

        // Don't put the next paragraph in the blockquote we just created for this paragraph unless
        // the next paragraph is in the same cell.
        if (enclosingCell && enclosingCell != enclosingNodeOfType(endOfNextParagraph.deepEquivalent(), &isTableCell))
            blockquoteForNextIndent = nullptr;

        // indentIntoBlockquote could move more than one paragraph if the paragraph
        // is in a list item or a table. As a result, endAfterSelection could refer to a position
        // no longer in the document.
        if (endAfterSelection.isNotNull() && !endAfterSelection.deepEquivalent().inShadowIncludingDocument())
            break;
        // Sanity check: Make sure our moveParagraph calls didn't remove endOfNextParagraph.deepEquivalent().anchorNode()
        // If somehow, e.g. mutation event handler, we did, return to prevent crashes.
        if (endOfNextParagraph.isNotNull() && !endOfNextParagraph.deepEquivalent().inShadowIncludingDocument())
            return;
        endOfCurrentParagraph = endOfNextParagraph;
    }
}

static bool isNewLineAtPosition(const Position& position)
{
    Node* textNode = position.computeContainerNode();
    int offset = position.offsetInContainerNode();
    if (!textNode || !textNode->isTextNode() || offset < 0 || offset >= textNode->maxCharacterOffset())
        return false;

    TrackExceptionState exceptionState;
    String textAtPosition = toText(textNode)->substringData(offset, 1, exceptionState);
    if (exceptionState.hadException())
        return false;

    return textAtPosition[0] == '\n';
}

static const ComputedStyle* computedStyleOfEnclosingTextNode(const Position& position)
{
    if (!position.isOffsetInAnchor() || !position.computeContainerNode() || !position.computeContainerNode()->isTextNode())
        return 0;
    return position.computeContainerNode()->computedStyle();
}

void ApplyBlockElementCommand::rangeForParagraphSplittingTextNodesIfNeeded(const VisiblePosition& endOfCurrentParagraph, Position& start, Position& end)
{
    start = startOfParagraph(endOfCurrentParagraph).deepEquivalent();
    end = endOfCurrentParagraph.deepEquivalent();

    document().updateStyleAndLayoutTree();

    bool isStartAndEndOnSameNode = false;
    if (const ComputedStyle* startStyle = computedStyleOfEnclosingTextNode(start)) {
        isStartAndEndOnSameNode = computedStyleOfEnclosingTextNode(end) && start.computeContainerNode() == end.computeContainerNode();
        bool isStartAndEndOfLastParagraphOnSameNode = computedStyleOfEnclosingTextNode(m_endOfLastParagraph) && start.computeContainerNode() == m_endOfLastParagraph.computeContainerNode();

        // Avoid obtanining the start of next paragraph for start
        // TODO(yosin) We should use |PositionMoveType::CodePoint| for
        // |previousPositionOf()|.
        if (startStyle->preserveNewline() && isNewLineAtPosition(start) && !isNewLineAtPosition(previousPositionOf(start, PositionMoveType::CodeUnit)) && start.offsetInContainerNode() > 0)
            start = startOfParagraph(createVisiblePosition(previousPositionOf(end, PositionMoveType::CodeUnit))).deepEquivalent();

        // If start is in the middle of a text node, split.
        if (!startStyle->collapseWhiteSpace() && start.offsetInContainerNode() > 0) {
            int startOffset = start.offsetInContainerNode();
            Text* startText = toText(start.computeContainerNode());
            splitTextNode(startText, startOffset);
            start = Position::firstPositionInNode(startText);
            if (isStartAndEndOnSameNode) {
                DCHECK_GE(end.offsetInContainerNode(), startOffset);
                end = Position(startText, end.offsetInContainerNode() - startOffset);
            }
            if (isStartAndEndOfLastParagraphOnSameNode) {
                DCHECK_GE(m_endOfLastParagraph.offsetInContainerNode(), startOffset);
                m_endOfLastParagraph = Position(startText, m_endOfLastParagraph.offsetInContainerNode() - startOffset);
            }
        }
    }

    document().updateStyleAndLayoutTree();

    if (const ComputedStyle* endStyle = computedStyleOfEnclosingTextNode(end)) {
        bool isEndAndEndOfLastParagraphOnSameNode = computedStyleOfEnclosingTextNode(m_endOfLastParagraph) && end.anchorNode() == m_endOfLastParagraph.anchorNode();
        // Include \n at the end of line if we're at an empty paragraph
        if (endStyle->preserveNewline() && start == end && end.offsetInContainerNode() < end.computeContainerNode()->maxCharacterOffset()) {
            int endOffset = end.offsetInContainerNode();
            // TODO(yosin) We should use |PositionMoveType::CodePoint| for
            // |previousPositionOf()|.
            if (!isNewLineAtPosition(previousPositionOf(end, PositionMoveType::CodeUnit)) && isNewLineAtPosition(end))
                end = Position(end.computeContainerNode(), endOffset + 1);
            if (isEndAndEndOfLastParagraphOnSameNode && end.offsetInContainerNode() >= m_endOfLastParagraph.offsetInContainerNode())
                m_endOfLastParagraph = end;
        }

        // If end is in the middle of a text node, split.
        if (endStyle->userModify() != READ_ONLY && !endStyle->collapseWhiteSpace() && end.offsetInContainerNode() && end.offsetInContainerNode() < end.computeContainerNode()->maxCharacterOffset()) {
            Text* endContainer = toText(end.computeContainerNode());
            splitTextNode(endContainer, end.offsetInContainerNode());
            if (isStartAndEndOnSameNode)
                start = firstPositionInOrBeforeNode(endContainer->previousSibling());
            if (isEndAndEndOfLastParagraphOnSameNode) {
                if (m_endOfLastParagraph.offsetInContainerNode() == end.offsetInContainerNode())
                    m_endOfLastParagraph = lastPositionInOrAfterNode(endContainer->previousSibling());
                else
                    m_endOfLastParagraph = Position(endContainer, m_endOfLastParagraph.offsetInContainerNode() - end.offsetInContainerNode());
            }
            end = Position::lastPositionInNode(endContainer->previousSibling());
        }
    }
}

VisiblePosition ApplyBlockElementCommand::endOfNextParagrahSplittingTextNodesIfNeeded(VisiblePosition& endOfCurrentParagraph, Position& start, Position& end)
{
    VisiblePosition endOfNextParagraph = endOfParagraph(nextPositionOf(endOfCurrentParagraph));
    Position position = endOfNextParagraph.deepEquivalent();
    const ComputedStyle* style = computedStyleOfEnclosingTextNode(position);
    if (!style)
        return endOfNextParagraph;

    Text* text = toText(position.computeContainerNode());
    if (!style->preserveNewline() || !position.offsetInContainerNode() || !isNewLineAtPosition(Position::firstPositionInNode(text)))
        return endOfNextParagraph;

    // \n at the beginning of the text node immediately following the current paragraph is trimmed by moveParagraphWithClones.
    // If endOfNextParagraph was pointing at this same text node, endOfNextParagraph will be shifted by one paragraph.
    // Avoid this by splitting "\n"
    splitTextNode(text, 1);

    if (text == start.computeContainerNode() && text->previousSibling() && text->previousSibling()->isTextNode()) {
        DCHECK_LT(start.offsetInContainerNode(), position.offsetInContainerNode());
        start = Position(toText(text->previousSibling()), start.offsetInContainerNode());
    }
    if (text == end.computeContainerNode() && text->previousSibling() && text->previousSibling()->isTextNode()) {
        DCHECK_LT(end.offsetInContainerNode(), position.offsetInContainerNode());
        end = Position(toText(text->previousSibling()), end.offsetInContainerNode());
    }
    if (text == m_endOfLastParagraph.computeContainerNode()) {
        if (m_endOfLastParagraph.offsetInContainerNode() < position.offsetInContainerNode()) {
            // We can only fix endOfLastParagraph if the previous node was still text and hasn't been modified by script.
            if (text->previousSibling()->isTextNode()
                && static_cast<unsigned>(m_endOfLastParagraph.offsetInContainerNode()) <= toText(text->previousSibling())->length())
                m_endOfLastParagraph = Position(toText(text->previousSibling()), m_endOfLastParagraph.offsetInContainerNode());
        } else {
            m_endOfLastParagraph = Position(text, m_endOfLastParagraph.offsetInContainerNode() - 1);
        }
    }

    return createVisiblePosition(Position(text, position.offsetInContainerNode() - 1));
}

HTMLElement* ApplyBlockElementCommand::createBlockElement() const
{
    HTMLElement* element = createHTMLElement(document(), m_tagName);
    if (m_inlineStyle.length())
        element->setAttribute(styleAttr, m_inlineStyle);
    return element;
}

DEFINE_TRACE(ApplyBlockElementCommand)
{
    visitor->trace(m_endOfLastParagraph);
    CompositeEditCommand::trace(visitor);
}

} // namespace blink
