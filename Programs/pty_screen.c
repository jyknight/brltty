/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2022 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://brltty.app/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

#include "prologue.h"

#include "log.h"
#include "pty_screen.h"
#include "scr_emulator.h"
#include "msg_queue.h"

static unsigned char screenLogLevel = LOG_DEBUG;

void
ptySetScreenLogLevel (unsigned char level) {
  screenLogLevel = level;
}

static unsigned char hasColors = 0;
static unsigned char currentForegroundColor;
static unsigned char currentBackgroundColor;
static unsigned char defaultForegroundColor;
static unsigned char defaultBackgroundColor;
static unsigned char colorPairMap[0100];

static unsigned char
toColorPair (unsigned char foreground, unsigned char background) {
  return colorPairMap[(background << 3) | foreground];
}

static void
initializeColors (unsigned char foreground, unsigned char background) {
  currentForegroundColor = defaultForegroundColor = foreground;
  currentBackgroundColor = defaultBackgroundColor = background;
}

static void
initializeColorPairs (void) {
  for (unsigned int pair=0; pair<ARRAY_COUNT(colorPairMap); pair+=1) {
    colorPairMap[pair] = pair;
  }

  {
    short foreground, background;
    pair_content(0, &foreground, &background);
    initializeColors(foreground, background);

    unsigned char pair = toColorPair(foreground, background);
    colorPairMap[pair] = 0;
    colorPairMap[0] = pair;
  }

  for (unsigned char foreground=COLOR_BLACK; foreground<=COLOR_WHITE; foreground+=1) {
    for (unsigned char background=COLOR_BLACK; background<=COLOR_WHITE; background+=1) {
      unsigned char pair = toColorPair(foreground, background);
      if (!pair) continue;
      init_pair(pair, foreground, background);
    }
  }
}

static int haveTerminalMessageQueue = 0;
static int terminalMessageQueue;
static int haveTerminalInputHandler = 0;

static int
sendTerminalMessage (MessageType type, const void *content, size_t length) {
  if (!haveTerminalMessageQueue) return 0;
  return sendMessage(terminalMessageQueue, type, content, length, 0);
}

static int
startTerminalMessageReceiver (const char *name, MessageType type, size_t size, MessageHandler *handler, void *data) {
  if (!haveTerminalMessageQueue) return 0;
  return startMessageReceiver(name, terminalMessageQueue, type, size, handler, data);
}

static void
messageHandler_terminalInput (const MessageHandlerParameters *parameters) {
  PtyObject *pty = parameters->data;
  const unsigned char *content = parameters->content;
  size_t length = parameters->length;

  ptyWriteInput(pty, content, length);
}

static void
enableMessages (key_t key) {
  haveTerminalMessageQueue = createMessageQueue(&terminalMessageQueue, key);
}

static int segmentIdentifier = 0;
static ScreenSegmentHeader *segmentHeader = NULL;

static int
destroySegment (void) {
  if (haveTerminalMessageQueue) {
    destroyMessageQueue(terminalMessageQueue);
    haveTerminalMessageQueue = 0;
  }

  return destroyScreenSegment(segmentIdentifier);
}

static int
createSegment (const char *path) {
  key_t key;

  if (makeTerminalKey(&key, path)) {
    segmentHeader = createScreenSegment(&segmentIdentifier, key, COLS, LINES);

    if (segmentHeader) {
      enableMessages(key);
      return 1;
    }
  }

  return 0;
}

static void
storeCursorPosition (void) {
  segmentHeader->cursorRow = getcury(stdscr);
  segmentHeader->cursorColumn = getcurx(stdscr);
}

static void
setColor (ScreenSegmentColor *ssc, unsigned char color, unsigned char level) {
  if (color & COLOR_RED) ssc->red = level;
  if (color & COLOR_GREEN) ssc->green = level;
  if (color & COLOR_BLUE) ssc->blue = level;
}

static ScreenSegmentCharacter *
setCharacter (unsigned int row, unsigned int column, ScreenSegmentCharacter **end) {
  cchar_t wch;

  {
    unsigned int oldRow = segmentHeader->cursorRow;
    unsigned int oldColumn = segmentHeader->cursorColumn;
    int move = (row != oldRow) || (column != oldColumn);

    if (move) ptySetCursorPosition(row, column);
    in_wch(&wch);
    if (move) ptySetCursorPosition(oldRow, oldColumn);
  }

  ScreenSegmentCharacter character = {
    .text = wch.chars[0],
  };

  {
    short fgColor, bgColor;
    pair_content(wch.ext_color, &fgColor, &bgColor);

    unsigned char bgLevel = SCREEN_SEGMENT_COLOR_LEVEL;
    unsigned char fgLevel = bgLevel;

    if (wch.attr & (A_BOLD | A_STANDOUT)) fgLevel = 0XFF;
    if (wch.attr & A_DIM) fgLevel >>= 1, bgLevel >>= 1;

    {
      ScreenSegmentColor *cfg, *cbg;

      if (wch.attr & A_REVERSE) {
        cfg = &character.background;
        cbg = &character.foreground;
      } else {
        cfg = &character.foreground;
        cbg = &character.background;
      }

      setColor(cfg, fgColor, fgLevel);
      setColor(cbg, bgColor, bgLevel);
    }
  }

  if (wch.attr & A_BLINK) character.blink = 1;
  if (wch.attr & A_UNDERLINE) character.underline = 1;

  {
    ScreenSegmentCharacter *location = getScreenCharacter(segmentHeader, row, column, end);
    *location = character;
    return location;
  }
}

static ScreenSegmentCharacter *
setCurrentCharacter (ScreenSegmentCharacter **end) {
  return setCharacter(segmentHeader->cursorRow, segmentHeader->cursorColumn, end);
}

static ScreenSegmentCharacter *
getCurrentCharacter (ScreenSegmentCharacter **end) {
  return getScreenCharacter(segmentHeader, segmentHeader->cursorRow, segmentHeader->cursorColumn, end);
}

static void
fillCharacters (unsigned int row, unsigned int column, unsigned int count) {
  ScreenSegmentCharacter *from = setCharacter(row, column, NULL);
  propagateScreenCharacter(from, (from + count));
}

static void
moveRows (unsigned int from, unsigned int to, unsigned int count) {
  if (count && (from != to)) {
    moveScreenCharacters(
      getScreenRow(segmentHeader, to, NULL),
      getScreenRow(segmentHeader, from, NULL),
      (count * COLS)
    );
  }
}

static void
fillRows (unsigned int row, unsigned int count) {
  fillCharacters(row, 0, (count * COLS));
}

static unsigned int scrollRegionTop;
static unsigned int scrollRegionBottom;

static unsigned int savedCursorRow = 0;
static unsigned int savedCursorColumn = 0;

int
ptyBeginScreen (PtyObject *pty) {
  haveTerminalMessageQueue = 0;
  haveTerminalInputHandler = 0;

  if (initscr()) {
    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE);

    raw();
    noecho();

    scrollok(stdscr, TRUE);
    idlok(stdscr, TRUE);
    idcok(stdscr, TRUE);

    scrollRegionTop = getbegy(stdscr);
    scrollRegionBottom = getmaxy(stdscr) - 1;

    savedCursorRow = 0;
    savedCursorColumn = 0;

    hasColors = has_colors();
    initializeColors(COLOR_WHITE, COLOR_BLACK);

    if (hasColors) {
      start_color();
      initializeColorPairs();
    }

    if (createSegment(ptyGetPath(pty))) {
      storeCursorPosition();

      haveTerminalInputHandler = startTerminalMessageReceiver(
        "terminal-input-receiver", TERMINAL_MESSAGE_INPUT,
        0X200, messageHandler_terminalInput, pty
      );

      return 1;
    }

    endwin();
  }

  return 0;
}

void
ptyEndScreen (void) {
  endwin();
  detachScreenSegment(segmentHeader);
  destroySegment();
}

void
ptyRefreshScreen (void) {
  sendTerminalMessage(TERMINAL_MESSAGE_UPDATED, NULL, 0);
  refresh();
}

void
ptySetCursorPosition (unsigned int row, unsigned int column) {
  move(row, column);
  storeCursorPosition();
}

void
ptySetCursorRow (unsigned int row) {
  ptySetCursorPosition(row, segmentHeader->cursorColumn);
}

void
ptySetCursorColumn (unsigned int column) {
  ptySetCursorPosition(segmentHeader->cursorRow, column);
}

void
ptySaveCursorPosition (void) {
  savedCursorRow = segmentHeader->cursorRow;
  savedCursorColumn = segmentHeader->cursorColumn;
}

void
ptyRestoreCursorPosition (void) {
  ptySetCursorPosition(savedCursorRow, savedCursorColumn);
}

void
ptySetScrollRegion (unsigned int top, unsigned int bottom) {
  scrollRegionTop = top;
  scrollRegionBottom = bottom;
  setscrreg(top, bottom);
}

static int
isWithinScrollRegion (unsigned int row) {
  if (row < scrollRegionTop) return 0;
  if (row > scrollRegionBottom) return 0;
  return 1;
}

int
ptyAmWithinScrollRegion (void) {
  return isWithinScrollRegion(segmentHeader->cursorRow);
}

void
ptyScrollBackward (unsigned int count) {
  unsigned int row = scrollRegionTop;
  unsigned int end = scrollRegionBottom + 1;
  unsigned int size = end - row;

  if (count > size) count = size;
  scrl(-count);

  moveRows(row, (row + count), (size - count));
  fillRows(row, count);
}

void
ptyScrollForward (unsigned int count) {
  unsigned int row = scrollRegionTop;
  unsigned int end = scrollRegionBottom + 1;
  unsigned int size = end - row;

  if (count > size) count = size;
  scrl(count);

  moveRows((row + count), row, (size - count));
  fillRows((end - count), count);
}

void
ptyMoveCursorUp (unsigned int amount) {
  unsigned int row = segmentHeader->cursorRow;
  if (amount > row) amount = row;
  if (amount > 0) ptySetCursorRow(row-amount);
}

void
ptyMoveCursorDown (unsigned int amount) {
  unsigned int oldRow = segmentHeader->cursorRow;
  unsigned int newRow = MIN(oldRow+amount, LINES-1);
  if (newRow != oldRow) ptySetCursorRow(newRow);
}

void
ptyMoveCursorLeft (unsigned int amount) {
  unsigned int column = segmentHeader->cursorColumn;
  if (amount > column) amount = column;
  if (amount > 0) ptySetCursorColumn(column-amount);
}

void
ptyMoveCursorRight (unsigned int amount) {
  unsigned int oldColumn = segmentHeader->cursorColumn;
  unsigned int newColumn = MIN(oldColumn+amount, COLS-1);
  if (newColumn != oldColumn) ptySetCursorColumn(newColumn);
}

void
ptyMoveUp1 (void) {
  if (segmentHeader->cursorRow == scrollRegionTop) {
    ptyScrollBackward(1);
  } else {
    ptyMoveCursorUp(1);
  }
}

void
ptyMoveDown1 (void) {
  if (segmentHeader->cursorRow == scrollRegionBottom) {
    ptyScrollForward(1);
  } else {
    ptyMoveCursorDown(1);
  }
}

void
ptyTabBackward (void) {
  ptySetCursorColumn(((segmentHeader->cursorColumn - 1) / TABSIZE) * TABSIZE);
}

void
ptyTabForward (void) {
  ptySetCursorColumn(((segmentHeader->cursorColumn / TABSIZE) + 1) * TABSIZE);
}

void
ptyInsertLines (unsigned int count) {
  if (ptyAmWithinScrollRegion()) {
    unsigned int row = segmentHeader->cursorRow;
    unsigned int oldTop = scrollRegionTop;
    unsigned int oldBottom = scrollRegionBottom;

    ptySetScrollRegion(row, scrollRegionBottom);
    ptyScrollBackward(count);
    ptySetScrollRegion(oldTop, oldBottom);
  }
}

void
ptyDeleteLines (unsigned int count) {
  if (ptyAmWithinScrollRegion()) {
    unsigned int row = segmentHeader->cursorRow;
    unsigned int oldTop = scrollRegionTop;
    unsigned int oldBottom = scrollRegionBottom;

    ptySetScrollRegion(row, scrollRegionBottom);
    ptyScrollForward(count);
    ptySetScrollRegion(oldTop, oldBottom);
  }
}

void
ptyInsertCharacters (unsigned int count) {
  ScreenSegmentCharacter *end;
  ScreenSegmentCharacter *from = getCurrentCharacter(&end);

  if ((from + count) > end) count = end - from;
  ScreenSegmentCharacter *to = from + count;
  moveScreenCharacters(to, from, (end - to));

  {
    unsigned int counter = count;
    while (counter-- > 0) insch(' ');
  }

  fillCharacters(segmentHeader->cursorRow, segmentHeader->cursorColumn, count);
}

void
ptyDeleteCharacters (unsigned int count) {
  ScreenSegmentCharacter *end;
  ScreenSegmentCharacter *to = getCurrentCharacter(&end);

  if ((to + count) > end) count = end - to;
  ScreenSegmentCharacter *from = to + count;
  if (from < end) moveScreenCharacters(to, from, (end - from));

  {
    unsigned int counter = count;
    while (counter-- > 0) delch();
  }

  fillCharacters(segmentHeader->cursorRow, (COLS - count), count);
}

void
ptyAddCharacter (unsigned char character) {
  unsigned int row = segmentHeader->cursorRow;
  unsigned int column = segmentHeader->cursorColumn;

  addch(character);
  storeCursorPosition();

  setCharacter(row, column, NULL);
}

void
ptySetCursorVisibility (unsigned int visibility) {
  curs_set(visibility);
}

void
ptySetAttributes (attr_t attributes) {
  attrset(attributes);
}

void
ptyAddAttributes (attr_t attributes) {
  attron(attributes);
}

void
ptyRemoveAttributes (attr_t attributes) {
  attroff(attributes);
}

static void
setCharacterColors (void) {
  attroff(A_COLOR);
  attron(COLOR_PAIR(toColorPair(currentForegroundColor, currentBackgroundColor)));
}

void
ptySetForegroundColor (int color) {
  if (color == -1) color = defaultForegroundColor;
  currentForegroundColor = color;
  setCharacterColors();
}

void
ptySetBackgroundColor (int color) {
  if (color == -1) color = defaultBackgroundColor;
  currentBackgroundColor = color;
  setCharacterColors();
}

void
ptyClearToEndOfDisplay (void) {
  clrtobot();

  ScreenSegmentCharacter *from = setCurrentCharacter(NULL);
  const ScreenSegmentCharacter *to = getScreenEnd(segmentHeader);
  propagateScreenCharacter(from, to);
}

void
ptyClearToEndOfLine (void) {
  clrtoeol();

  ScreenSegmentCharacter *to;
  ScreenSegmentCharacter *from = setCurrentCharacter(&to);
  propagateScreenCharacter(from, to);
}

void
ptyClearToBeginningOfLine (void) {
  unsigned int column = segmentHeader->cursorColumn;
  if (column > 0) ptySetCursorColumn(0);

  while (1) {
    ptyAddCharacter(' ');
    if (segmentHeader->cursorColumn > column) break;
  }

  ptySetCursorColumn(column);
}