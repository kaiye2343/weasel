#include "stdafx.h"
#include "HorizontalLayout.h"

using namespace weasel;

void HorizontalLayout::DoLayout(CDCHandle dc, PDWR pDWR) {
  CSize size;
  int width = offsetX + real_margin_x, height = offsetY + real_margin_y;
  int w = offsetX + real_margin_x;

  /* calc mark_text sizes */
  if ((_style.hilited_mark_color & 0xff000000)) {
    CSize sg;
    if (candidates_count) {
      if (_style.mark_text.empty())
        GetTextSizeDW(L"|", 1, pDWR->pTextFormat, pDWR, &sg);
      else
        GetTextSizeDW(_style.mark_text, _style.mark_text.length(),
                      pDWR->pTextFormat, pDWR, &sg);
    }

    mark_width = sg.cx;
    mark_height = sg.cy;
    if (_style.mark_text.empty()) {
      mark_width = mark_height / 7;
      if (_style.linespacing && _style.baseline)
        mark_width =
            (int)((float)mark_width / ((float)_style.linespacing / 100.0f));
      mark_width = max(mark_width, 6);
    }
    mark_gap = (_style.mark_text.empty()) ? mark_width
                                          : mark_width + _style.hilite_spacing;
  }
  int base_offset = ((_style.hilited_mark_color & 0xff000000)) ? mark_gap : 0;

  // calc page indicator
  CSize pgszl, pgszr;
  if (!IsInlinePreedit()) {
    GetTextSizeDW(pre, pre.length(), pDWR->pPreeditTextFormat, pDWR, &pgszl);
    GetTextSizeDW(next, next.length(), pDWR->pPreeditTextFormat, pDWR, &pgszr);
  }
  bool page_en = (_style.prevpage_color & 0xff000000) &&
                 (_style.nextpage_color & 0xff000000);
  int pgw = page_en ? (pgszl.cx + pgszr.cx + _style.hilite_spacing +
                       _style.hilite_padding_x * 2)
                    : 0;
  int pgh = page_en ? max(pgszl.cy, pgszr.cy) : 0;

  /* Preedit */
  if (!IsInlinePreedit() && !_context.preedit.str.empty()) {
    size = GetPreeditSize(dc, _context.preedit, pDWR->pPreeditTextFormat, pDWR);
    int szx = pgw, szy = max(size.cy, pgh);
    // icon size higher then preedit text
    int yoffset = (STATUS_ICON_SIZE >= szy && ShouldDisplayStatusIcon())
                      ? (STATUS_ICON_SIZE - szy) / 2
                      : 0;
    _preeditRect.SetRect(w, height + yoffset, w + size.cx,
                         height + yoffset + size.cy);
    height += szy + 2 * yoffset + _style.spacing;
    width = max(width, real_margin_x * 2 + size.cx + szx);
    if (ShouldDisplayStatusIcon())
      width += STATUS_ICON_SIZE;
  }

  /* Auxiliary */
  if (!_context.aux.str.empty()) {
    size = GetPreeditSize(dc, _context.aux, pDWR->pPreeditTextFormat, pDWR);
    // icon size higher then auxiliary text
    int yoffset = (STATUS_ICON_SIZE >= size.cy && ShouldDisplayStatusIcon())
                      ? (STATUS_ICON_SIZE - size.cy) / 2
                      : 0;
    _auxiliaryRect.SetRect(w, height + yoffset, w + size.cx,
                           height + yoffset + size.cy);
    height += size.cy + 2 * yoffset + _style.spacing;
    width = max(width, real_margin_x * 2 + size.cx);
  }

  int row_cnt = 0;
  int row_of_candidate[MAX_CANDIDATES_COUNT] = {0};
  annotation_separators.clear();
  const int count = min(candidates_count, MAX_CANDIDATES_COUNT);
  if (count) {
    CSize labelSizes[MAX_CANDIDATES_COUNT], textSizes[MAX_CANDIDATES_COUNT], commentSizes[MAX_CANDIDATES_COUNT];
    int blockWidths[MAX_CANDIDATES_COUNT] = {0};
    const bool above = _style.annotation_position == 1;
    for (int i = 0; i < count; ++i) {
      auto label = GetLabelText(labels, i, _style.label_text_format.c_str());
      GetTextSizeDW(label, label.length(), pDWR->pLabelTextFormat, pDWR, &labelSizes[i]);
      labelSizes[i].cx *= labelFontValid; labelSizes[i].cy *= labelFontValid;
      const auto& text = candidates[i].str;
      GetTextSizeDW(text, text.length(), pDWR->pTextFormat, pDWR, &textSizes[i]);
      textSizes[i].cx *= textFontValid; textSizes[i].cy *= textFontValid;
      commentSizes[i] = CSize(0, 0);
      bool visible = cmtFontValid && ((i == id ? _style.hilited_comment_text_color : _style.comment_text_color) & 0xff000000);
      if (_style.annotation_position != 2 && i < (int)comments.size() && !comments[i].empty() && visible)
        GetTextSizeDW(comments[i].str, comments[i].str.length(), pDWR->pCommentTextFormat, pDWR, &commentSizes[i]);
      int textWidth = labelSizes[i].cx + _style.hilite_spacing + textSizes[i].cx;
      blockWidths[i] = above ? max(textWidth, commentSizes[i].cx) : textWidth + (commentSizes[i].cx ? _style.hilite_spacing + commentSizes[i].cx : 0);
    }
    CSize blankComment(0, 0);
    if (above && cmtFontValid) GetTextSizeDW(L"ā", 1, pDWR->pCommentTextFormat, pDWR, &blankComment);
    int first = 0;
    while (first < count) {
      int last = first, rowWidth = 0, textHeight = 0, commentHeight = 0;
      for (; last < count; ++last) {
        int cell = blockWidths[last] + (last == id ? base_offset : 0);
        int proposed = rowWidth + (last > first ? _style.candidate_spacing : 0) + cell;
        if (_style.max_width > 0 && last > first && proposed + real_margin_x * 2 > _style.max_width) break;
        rowWidth = proposed;
        textHeight = max(textHeight, max(labelSizes[last].cy, textSizes[last].cy));
        commentHeight = max(commentHeight, commentSizes[last].cy);
      }
      bool hasAnnotation = above && cmtFontValid && (commentHeight > 0 || !_style.annotation_collapse_empty);
      if (hasAnnotation) commentHeight = max(commentHeight, blankComment.cy);
      int gap = hasAnnotation ? max(_style.annotation_gap, _style.annotation_separator_enabled ? _style.annotation_separator_thickness : 0) : 0;
      int rowHeight = above ? textHeight + (hasAnnotation ? commentHeight + gap : 0) : max(textHeight, commentHeight);
      int rowLeft = offsetX + real_margin_x;
      w = rowLeft;
      for (int i = first; i < last; ++i) {
        if (i > first) w += _style.candidate_spacing;
        int cellLeft = w;
        if (i == id) w += base_offset;
        int textWidth = labelSizes[i].cx + _style.hilite_spacing + textSizes[i].cx;
        int textX = w + (above ? (blockWidths[i] - textWidth) / 2 : 0);
        auto lineY = [&](int itemHeight) {
          if (above) return height + rowHeight - itemHeight;
          return height + (_style.align_type == UIStyle::ALIGN_CENTER ? (rowHeight - itemHeight) / 2 : _style.align_type == UIStyle::ALIGN_BOTTOM ? rowHeight - itemHeight : 0);
        };
        int labelY = lineY(labelSizes[i].cy), textY = lineY(textSizes[i].cy);
        _candidateLabelRects[i].SetRect(textX, labelY, textX + labelSizes[i].cx, labelY + labelSizes[i].cy);
        textX += labelSizes[i].cx + _style.hilite_spacing;
        _candidateTextRects[i].SetRect(textX, textY, textX + textSizes[i].cx, textY + textSizes[i].cy);
        if (commentSizes[i].cx) {
          int commentX = above ? w + (blockWidths[i] - commentSizes[i].cx) / 2 : textX + textSizes[i].cx + _style.hilite_spacing;
          int commentY = above ? height + commentHeight - commentSizes[i].cy : lineY(commentSizes[i].cy);
          _candidateCommentRects[i].SetRect(commentX, commentY, commentX + commentSizes[i].cx, commentY + commentSizes[i].cy);
        } else _candidateCommentRects[i].SetRectEmpty();
        w += blockWidths[i];
        _candidateRects[i].SetRect(cellLeft, height, w, height + rowHeight);
        row_of_candidate[i] = row_cnt;
      }
      if (hasAnnotation && _style.annotation_separator_enabled) {
        int lineTop = height + commentHeight + (gap - _style.annotation_separator_thickness) / 2;
        annotation_separators.push_back(CRect(rowLeft, lineTop, w, lineTop + max(1, _style.annotation_separator_thickness)));
      }
      width = max(width, w);
      height += rowHeight;
      first = last;
      if (first < count) { height += _style.candidate_spacing; ++row_cnt; }
    }
    height -= offsetY;
  } else {
    height -= _style.spacing + offsetY;
    width += _style.hilite_spacing + _style.border;
  }

  width += real_margin_x;
  height += real_margin_y;

  if (candidates_count) {
    width = max(width, _style.min_width);
    height = max(height, _style.min_height);
  }
  if (candidates_count) {
    for (auto i = 0; i < candidates_count && i < MAX_CANDIDATES_COUNT; ++i) {
      // make rightest candidate's rect right the same for better look
      if ((i < candidates_count - 1 &&
           row_of_candidate[i] < row_of_candidate[i + 1]) ||
          (i == candidates_count - 1))
        _candidateRects[i].right = width - real_margin_x;
    }
  }
  _highlightRect = _candidateRects[id];
  UpdateStatusIconLayout(&width, &height);
  _contentSize.SetSize(width + offsetX, height + 2 * offsetY);
  _contentRect.SetRect(0, 0, _contentSize.cx, _contentSize.cy);

  // calc page indicator
  if (page_en && candidates_count && !_style.inline_preedit) {
    int _prex = _contentSize.cx - offsetX - real_margin_x +
                _style.hilite_padding_x - pgw;
    int _prey = (_preeditRect.top + _preeditRect.bottom) / 2 - pgszl.cy / 2;
    _prePageRect.SetRect(_prex, _prey, _prex + pgszl.cx, _prey + pgszl.cy);
    _nextPageRect.SetRect(_prePageRect.right + _style.hilite_spacing, _prey,
                          _prePageRect.right + _style.hilite_spacing + pgszr.cx,
                          _prey + pgszr.cy);
    if (ShouldDisplayStatusIcon()) {
      _prePageRect.OffsetRect(-STATUS_ICON_SIZE, 0);
      _nextPageRect.OffsetRect(-STATUS_ICON_SIZE, 0);
    }
  }

  // prepare temp rect _bgRect for roundinfo calculation
  if (_style.skin_padding.size() == 4) {
    int left = _style.skin_padding[0], top = _style.skin_padding[1];
    _preeditRect.OffsetRect(left, top);
    _auxiliaryRect.OffsetRect(left, top);
    _highlightRect.OffsetRect(left, top);
    _prePageRect.OffsetRect(left, top);
    _nextPageRect.OffsetRect(left, top);
    _statusIconRect.OffsetRect(left, top);
    for (auto& rect : annotation_separators) rect.OffsetRect(left, top);
    for (int i = 0; i < candidates_count && i < MAX_CANDIDATES_COUNT; ++i) {
      _candidateRects[i].OffsetRect(left, top);
      _candidateLabelRects[i].OffsetRect(left, top);
      _candidateTextRects[i].OffsetRect(left, top);
      _candidateCommentRects[i].OffsetRect(left, top);
    }
    _contentSize.cx += left + _style.skin_padding[2];
    _contentSize.cy += top + _style.skin_padding[3];
    _contentRect.SetRect(0, 0, _contentSize.cx, _contentSize.cy);
  }
  CopyRect(_bgRect, _contentRect);
  _bgRect.DeflateRect(offsetX + 1, offsetY + 1);
  // prepare round info for single row status, only for single row situation
  _PrepareRoundInfo(dc);
  // readjust for multi rows
  if (row_cnt)  // row_cnt > 0, at least 2 candidates
  {
    _roundInfo[0].IsBottomLeftNeedToRound = false;
    _roundInfo[candidates_count - 1].IsTopRightNeedToRound = false;
    for (auto i = 1; i < candidates_count; i++) {
      _roundInfo[i].Hemispherical = _roundInfo[0].Hemispherical;
      if (row_of_candidate[i] == row_cnt &&
          row_of_candidate[i - 1] == row_cnt - 1)
        _roundInfo[i].IsBottomLeftNeedToRound = true;
      if (i + 1 < candidates_count && i + 1 < MAX_CANDIDATES_COUNT &&
          row_of_candidate[i] == 0 && row_of_candidate[i + 1] == 1)
        _roundInfo[i].IsTopRightNeedToRound = _style.inline_preedit;
    }
  }
  // truely draw content size calculation
  _contentRect.DeflateRect(offsetX, offsetY);
}
