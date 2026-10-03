class BookMenu extends MovieClip
{
   // ---- Original properties ----
   var BookPages;
   var PageInfoA;
   var RefTextFieldTextFormat;
   var ReferenceTextField;
   var ReferenceTextInstance;
   var ReferenceText_mc;
   var bNote;
   var iCurrentLine;
   var iLeftPageNumber;
   var iMaxPageHeight;
   var iNextPageBreak;
   var iPageSetIndex;
   var iPaginationIndex;
   static var BookMenuInstance;
   // Read as plain text by the plugins (WritingMode.cpp; the SWF ships uncompressed): installed, it turns writing on.
   // Mod-neutral: Ink & Quill ships it for every client.  Bump when a call is added; a plugin needs at least its version.
   static var WRITING_INTERFACE = "BOOKMENU_WRITING_INTERFACE=4";
   // Text written in blood (2): dark red, marked in the plugin's text between these two
   // private-use characters (see EditBuildMarked, MarkBlood).
   static var BLOOD_COLOR = 0x2B0202;
   static var BLOOD_OPEN = 0xE000;
   static var BLOOD_CLOSE = 0xE001;
   // Marked text (4): what the client locks is between these two (see EditBuildMarked).
   static var LOCK_OPEN = 0xE002;
   static var LOCK_CLOSE = 0xE003;
   static var PAGE_BREAK_TAG = "[pagebreak]";
   // Headings: a line starting "# " or "## " is enlarged by these, its mark taken out (reading) or shrunk (writing).
   static var HEADING_SCALE = [1, 1.5, 1.25];
   static var NOTE_WIDTH = 400;
   static var NOTE_X_OFFSET = 20;
   static var NOTE_Y_OFFSET = 10;
   static var CACHED_PAGES = 4;
   static var EDIT_FIELD_HEIGHT = 20000;   // ~40 note pages before the edit field would scroll
   static var EDIT_MASK_OVERHANG = 12;
   static var SUGGEST_COLOR = 0x2A2520;   // a suggestion's faded ink (lighter is unreadable on the vanilla page)
   static var EDIT_KEY_TURN_BLOCK_MS = 200;
   //@variant convenient-reading
   // Convenient Reading's text sizes for books and notes, from its ini (its own code).
   static var FONT_SIZE_B = 18;
   static var FONT_SIZE_N = 18;
   //@end

   // ---- Edit mode properties ----
   var bEditMode;
   var iEditPage;        // page the caret is on
   var aEditPageTops;    // y of each page's first line in EditField (same rule as CalculatePagination), then the text's bottom
   var aEditPageLines;   // index of each page's first line
   var EditClip;         // Visible MovieClip used for editing (duplicated from ReferenceText_mc)
   var EditField;        // The TextField inside EditClip: the whole text, tall enough never to scroll
   var EditMask;         // shows one page of EditField
   var iSuppressTurnUntil;   // getTimer() before which engine page turns are refused (key presses)
   var iEditShownFrom;   // books: offset of the engine's current spread in its 4 page slots (0 or 2)
   var aSegs;            // the text as segments: {locked, body, editable} lengths, in order (see EditBuildMarked)
   var oContentFmt;      // entry text's format (config font, content size)
   var oBreakFmt;        // blank lines' format, as reading has them (see EditBuildMarked)
   var bTextReceived;    // SetBookText has run (EditReady)
   var sBookText;        // the text reading shows now (SetBookText): ReturnToReading with no text reads it again
   var bBlood;           // this writing session is in blood: typed text is red (EditSetBlood)
   var oEditMarked;      // marked text from the plugin (SetEditMarked) for the next edit mode: {text, font, size}
   var aEditPageBottoms; // y where each page's text ends (a [pagebreak] line is on neither page)

   function BookMenu()
   {
      super();
      BookMenu.BookMenuInstance = this;
      this.BookPages = new Array();
      this.PageInfoA = new Array();
      this.iLeftPageNumber = 0;
      this.iPageSetIndex = 0;
      this.bNote = false;
      this.bEditMode = false;
      this.iEditPage = 0;
      this.aEditPageTops = [2];
      this.ReferenceText_mc = this.ReferenceTextInstance;
      this.RefTextFieldTextFormat = this.ReferenceText_mc.PageTextField.getTextFormat();
      //@variant convenient-reading
      BookMenu.FONT_SIZE_B = BookMenu.FONT_SIZE_N = BookMenu.ValidSize(this.RefTextFieldTextFormat.size, 18);
      var _loc3_ = new LoadVars();
      _loc3_.load("../Convenient Reading.ini");
      // Without the ini this can still be called, with nothing usable: keep the defaults.
      _loc3_.onData = function(str)
      {
         BookMenu.FONT_SIZE_B = BookMenu.ValidSize(parseFloat(BookMenu.ParseConfig(str,"iBookFontSize")), BookMenu.FONT_SIZE_B);
         BookMenu.FONT_SIZE_N = BookMenu.ValidSize(parseFloat(BookMenu.ParseConfig(str,"iNoteFontSize")), BookMenu.FONT_SIZE_N);
      };
      //@end
   }

   // The book's text size: the page text field's own (vanilla 22); Convenient Reading's configured one in its variant.
   function PageTextSize()
   {
      //@variant convenient-reading
      return this.bNote ? BookMenu.FONT_SIZE_N : BookMenu.FONT_SIZE_B;
      //@end
      return BookMenu.ValidSize(this.RefTextFieldTextFormat.size, 22);
   }

   // The book's text as the page field takes it: as given (vanilla); in Convenient Reading's size in its variant.
   function PageHtml(text)
   {
      //@variant convenient-reading
      return "<font size='" + this.PageTextSize() + "'>" + text + "</font>";
      //@end
      return text;
   }

   // A usable text size, or the fallback (NaN or 0 would make text invisible).
   static function ValidSize(size, fallback)
   {
      return size == undefined || isNaN(size) || size <= 0 ? fallback : size;
   }

   function onLoad()
   {
      this.ReferenceText_mc._visible = false;
      this.ReferenceTextField = this.ReferenceText_mc.PageTextField;
      this.ReferenceTextField.noTranslate = true;
      this.ReferenceTextField.setTextFormat(this.RefTextFieldTextFormat);
      this.iMaxPageHeight = this.ReferenceTextField._height;
      //@variant convenient-reading
      var fieldSize = this.ReferenceTextField.getTextFormat().size;
      BookMenu.FONT_SIZE_B = BookMenu.ValidSize(BookMenu.FONT_SIZE_B, fieldSize);
      BookMenu.FONT_SIZE_N = BookMenu.ValidSize(BookMenu.FONT_SIZE_N, fieldSize);
      //@end

      // Original callbacks
      gfx.io.GameDelegate.addCallBack("SetBookText",this,"SetBookText");
      gfx.io.GameDelegate.addCallBack("TurnPage",this,"TurnPage");
      gfx.io.GameDelegate.addCallBack("PrepForClose",this,"PrepForClose");
   }

   // ======== EDIT MODE (docs/EDITING.md) ========
   // The plugin calls these through the movie; the book is open, so SetBookText has run and bNote is known.

   function EnterEditMode()
   {
      this.bEditMode = true;
      this.bBlood = false;
      // The page being read (at the book's opening: page 0). A book's spread is in engine
      // slots 0-1 or 2-3 (see iEditShownFrom); editing starts on the same page and slots.
      var readPage = this.iLeftPageNumber;
      var readShownFrom = this.bNote ? 0 : this.iLeftPageNumber - this.iPageSetIndex;
      // Hide ALL existing display pages
      var i = 0;
      while(i < this.BookPages.length)
      {
         this.BookPages[i]._visible = false;
         i++;
      }

      // Stop any ongoing pagination
      if(this.iPaginationIndex != -1)
      {
         clearInterval(this.iPaginationIndex);
         this.iPaginationIndex = -1;
      }

      // Duplicate the ReferenceText_mc to get a clip with the correct font embedding
      this.EditClip = this.ReferenceText_mc.duplicateMovieClip("EditClip", this.getNextHighestDepth());
      // Vanilla's reference clip has two frames and would keep looping in the copy.
      this.EditClip.gotoAndStop(1);
      this.EditField = this.EditClip.PageTextField;

      // Clear ALL text from the duplicated field (it copied the note's content)
      this.EditField.text = "";
      this.EditField.htmlText = "";
      this.EditField.replaceText(0, this.EditField.length, "");

      // Make it editable
      this.EditField.type = "input";
      this.EditField.selectable = true;
      this.EditField.noTranslate = true;
      this.EditField.multiline = true;
      this.EditField.wordWrap = true;
      this.EditField.autoSize = "none";
      this.EditField.verticalAutoSize = "none";

      // Apply handwriting font formatting
      var fmt = new TextFormat();
      fmt.font = "$HandwrittenFont";
      fmt.size = this.PageTextSize();
      // The page's own ink, as when reading.
      fmt.color = this.RefTextFieldTextFormat.color == undefined ? 0 : this.RefTextFieldTextFormat.color;
      fmt.letterSpacing = 0;
      fmt.kerning = true;
      this.EditField.setNewTextFormat(fmt);
      this.EditField.setTextFormat(fmt);

      // Position to match note layout (same as CreateDisplayPage does for notes)
      if(this.bNote)
      {
         this.EditField._width = BookMenu.NOTE_WIDTH;
         this.EditClip._x = Stage.visibleRect.x + BookMenu.NOTE_X_OFFSET;
         this.EditClip._y = Stage.visibleRect.y + BookMenu.NOTE_Y_OFFSET;
      }
      else
      {
         this.EditClip._x = this.ReferenceText_mc._x;
         this.EditClip._y = this.ReferenceText_mc._y;
      }
      // The whole text in one field that never scrolls (a scrolling input field
      // follows the caret a line at a time); the mask shows one page of it.
      this.EditField._height = BookMenu.EDIT_FIELD_HEIGHT;
      this.EditMask = this.createEmptyMovieClip("EditMask", this.getNextHighestDepth());
      this.EditClip.setMask(this.EditMask);
      this.EditClip._visible = true;

      this.iEditPage = 0;
      this.iEditShownFrom = 0;
      this.EditBuildMarked();
      var start = this.EditSnap(0, 1);
      this.EditSetCaretOrNone(start);
      this.EditLayout();
      // Start on the page being read. If it has nowhere to type (the title spread), turn to
      // the first entry's page, where the caret is; with no entry at all, stay.
      this.iEditShownFrom = readShownFrom;
      this.EditGoToPage(readPage);
      if(start >= 0)
      {
         var caretPage = this.PageOfPos(this.EditCaret());
         if(caretPage != this.iEditPage && (this.bNote || caretPage != this.iEditPage + 1))
         {
            this.EditLayout();
         }
      }
   }

   // ---- Content: locked text and editable bodies ----

   // From the plugin, before EnterEditMode (4): the book's own text as reading shows it, with what the player can't
   // change between LOCK_OPEN and LOCK_CLOSE.  font and size (optional): the format typed text takes, and paragraph
   // breaks get the page's outer size (FormatBreaks); without them typed text takes its neighbour's format.
   function SetEditMarked(text, font, size)
   {
      this.oEditMarked = {text:text, font:font, size:size};
   }

   // From the plugin, while writing marked text (4): the client's text rendered again (a new entry, a tear-out). The
   // caret goes to run caretRun at caretOffset (-1: its end), on its page. Returns the number of runs, or -1.
   function EditReload(text, font, size, caretRun, caretOffset, readingText)
   {
      if(!this.bEditMode || this.EditField == undefined)
      {
         return -1;
      }
      this.EditSuggestClear();
      // What reading shows if the edit key returns with nothing to save (a tear-out changes no run's text).
      if(readingText != undefined && readingText.length)
      {
         this.sBookText = readingText;
      }
      this.oEditMarked = {text:text, font:font, size:size};
      this.EditBuildMarked();
      var runs = 0;
      var target = -1;
      var k = 0;
      while(k < this.aSegs.length)
      {
         if(this.aSegs[k].editable)
         {
            if(runs == caretRun)
            {
               target = k;
            }
            runs++;
         }
         k++;
      }
      if(target >= 0)
      {
         var offset = caretOffset < 0 || caretOffset > this.aSegs[target].body ? this.aSegs[target].body : caretOffset;
         this.EditSetCaret(this.BodyStart(target) + offset);
      }
      else
      {
         this.EditSetCaretOrNone(this.EditSnap(0, 1));
      }
      this.EditLayout();
      return runs;
   }

   // The marked text laid out as SetBookText lays out the reading text, then the markers taken out: locked ranges,
   // editable runs between them (every gap between a close and the next open, even empty; text before the first lock
   // or after the last one only if there is any), blood ranges per run.
   function EditBuildMarked()
   {
      var m = this.oEditMarked;
      this.EditField.html = true;
      this.EditField.setNewTextFormat(this.RefTextFieldTextFormat);
      this.EditField.SetText(this.PageHtml(m.text), true);
      // Where each marker is in the text without markers, then take them out, last first.
      var raw = this.EditField.text;
      var marks = [];
      var removed = 0;
      var i = 0;
      var c;
      while(i < raw.length)
      {
         c = raw.charCodeAt(i);
         if(c == BookMenu.LOCK_OPEN || c == BookMenu.LOCK_CLOSE || c == BookMenu.BLOOD_OPEN || c == BookMenu.BLOOD_CLOSE)
         {
            marks.push({c:c, at:i - removed});
            removed++;
         }
         i++;
      }
      i = raw.length - 1;
      while(i >= 0)
      {
         c = raw.charCodeAt(i);
         if(c == BookMenu.LOCK_OPEN || c == BookMenu.LOCK_CLOSE || c == BookMenu.BLOOD_OPEN || c == BookMenu.BLOOD_CLOSE)
         {
            this.EditField.replaceText(i, i + 1, "");
         }
         i--;
      }
      var len = this.EditField.length;
      // Locks [s, e), in order; blood ranges in the whole text.
      var locks = [];
      var open = -1;
      var blood = [];
      var bOpen = -1;
      i = 0;
      while(i < marks.length)
      {
         if(marks[i].c == BookMenu.LOCK_OPEN)
         {
            open = marks[i].at;
         }
         else if(marks[i].c == BookMenu.LOCK_CLOSE)
         {
            locks.push({s:open < 0 ? 0 : open, e:marks[i].at});
            open = -1;
         }
         else if(marks[i].c == BookMenu.BLOOD_OPEN)
         {
            bOpen = marks[i].at;
         }
         else if(bOpen >= 0)
         {
            blood.push({s:bOpen, e:marks[i].at});
            bOpen = -1;
         }
         i++;
      }
      if(open >= 0)
      {
         locks.push({s:open, e:len});
      }
      // Segments {locked, body, editable}: a lock, then the run after it.
      var segs = [];
      if(!locks.length || locks[0].s > 0)
      {
         segs.push({locked:0, body:locks.length ? locks[0].s : len, editable:true});
      }
      i = 0;
      while(i < locks.length)
      {
         var next = i + 1 < locks.length ? locks[i + 1].s : len;
         segs.push({locked:locks[i].e - locks[i].s, body:next - locks[i].e, editable:i + 1 < locks.length || next > locks[i].e});
         i++;
      }
      this.aSegs = segs;
      // Locked [pagebreak]s become spaces in their own format: the line keeps its height, and its letters can't
      // show above the next page (reading cuts pages out; the editor masks one tall field).  Their places are kept.
      var blank = "";
      while(blank.length < BookMenu.PAGE_BREAK_TAG.length)
      {
         blank += " ";
      }
      var k = 0;
      while(k < segs.length)
      {
         segs[k].breaks = [];
         var segStart = this.SegStart(k);
         var at = this.EditField.text.indexOf(BookMenu.PAGE_BREAK_TAG, segStart);
         while(at >= 0 && at + blank.length <= segStart + segs[k].locked)
         {
            var tagFmt = this.EditField.getTextFormat(at, at + 1);
            this.EditField.replaceText(at, at + blank.length, blank);
            this.EditField.setTextFormat(at, at + blank.length, tagFmt);
            segs[k].breaks.push(at - segStart);
            at = this.EditField.text.indexOf(BookMenu.PAGE_BREAK_TAG, at + blank.length);
         }
         k++;
      }
      k = 0;
      while(k < segs.length)
      {
         var bs = this.BodyStart(k);
         var be = this.BodyEnd(k);
         var mine = [];
         var b = 0;
         while(b < blood.length)
         {
            if(blood[b].e > bs && blood[b].s < be)
            {
               mine.push({s:Math.max(blood[b].s, bs) - bs, e:Math.min(blood[b].e, be) - bs});
            }
            b++;
         }
         segs[k].blood = BookMenu.MergeRanges(mine);
         // The client marks blood rather than colouring it: paint it now (FormatBreaks only repaints edited runs).
         var red = new TextFormat();
         red.color = BookMenu.BLOOD_COLOR;
         var r = 0;
         while(r < segs[k].blood.length)
         {
            this.EditField.setTextFormat(bs + segs[k].blood[r].s, bs + segs[k].blood[r].e, red);
            r++;
         }
         k++;
      }
      this.oContentFmt = undefined;
      this.oBreakFmt = undefined;
      if(m.font != undefined && m.font.length && m.size > 0)
      {
         this.oContentFmt = new TextFormat();
         this.oContentFmt.font = m.font;
         this.oContentFmt.size = m.size;
         this.oBreakFmt = new TextFormat();
         this.oBreakFmt.font = this.RefTextFieldTextFormat.font;
         this.oBreakFmt.size = size;
      }
      // Headings: each run's base size as loaded (its first character's), then the heading lines styled, in locked
      // text too, so the editor shows what reading shows.
      k = 0;
      while(k < segs.length)
      {
         var first = this.BodyStart(k);
         segs[k].baseSize = this.BodyEnd(k) > first ? this.EditField.getTextFormat(first, first + 1).size : size;
         if(segs[k].locked > 0)
         {
            this.StyleLockedHeadings(k);
         }
         if(segs[k].editable)
         {
            this.StyleHeadings(k);
         }
         k++;
      }
   }

   // 1 for a line starting "# ", 2 for "## ", else 0.
   static function HeadingLevel(text, pos)
   {
      if(text.substr(pos, 2) == "# ")
      {
         return 1;
      }
      if(text.substr(pos, 3) == "## ")
      {
         return 2;
      }
      return 0;
   }

   // Reading: each heading line's mark taken out and its text enlarged, from the last line up so offsets hold.
   static function ReadHeadings(tf)
   {
      var text = tf.text;
      var starts = [0];
      var i = 0;
      while(i < text.length)
      {
         if(text.charAt(i) == "\r")
         {
            starts.push(i + 1);
         }
         i++;
      }
      var j = starts.length - 1;
      while(j >= 0)
      {
         var p = starts[j];
         var level = BookMenu.HeadingLevel(text, p);
         if(level > 0)
         {
            var mark = level + 1;
            var end = text.indexOf("\r", p);
            if(end < 0)
            {
               end = text.length;
            }
            var size = tf.getTextFormat(end > p + mark ? p + mark : p, end > p + mark ? p + mark + 1 : p + 1).size;
            tf.replaceText(p, p + mark, "");
            if(end - mark > p && size > 0)
            {
               var f = new TextFormat();
               f.size = Math.round(size * BookMenu.HEADING_SCALE[level]);
               tf.setTextFormat(p, end - mark, f);
            }
         }
         j--;
      }
   }

   // Writing: run k's heading lines enlarged from the run's base size, their marks kept but shrunk to nothing.  Without
   // the client's hint (FormatBreaks then resets nothing), its other lines go back to the base size here.
   function StyleHeadings(k)
   {
      var tf = this.EditField;
      var text = tf.text;
      var start = this.BodyStart(k);
      var end = this.BodyEnd(k);
      var base = this.oContentFmt != undefined ? this.oContentFmt.size : this.aSegs[k].baseSize;
      var reset = this.oContentFmt == undefined && base > 0;
      // A run that begins mid-line (after locked text) has no line start there.
      var lineStart = start == 0 || text.charAt(start - 1) == "\r";
      var p = start;
      while(p <= end)
      {
         var lineEnd = text.indexOf("\r", p);
         if(lineEnd < 0 || lineEnd > end)
         {
            lineEnd = end;
         }
         if(reset)
         {
            var plain = new TextFormat();
            plain.size = base;
            tf.setTextFormat(p, lineEnd < end ? lineEnd + 1 : lineEnd, plain);
         }
         var level = lineStart ? BookMenu.HeadingLevel(text, p) : 0;
         this.StyleHeadingLine(p, lineEnd, level, base);
         p = lineEnd + 1;
         lineStart = true;
      }
   }

   // Locked text's heading lines, once as loaded (it can't change), each from its own size as reading does.
   function StyleLockedHeadings(k)
   {
      var text = this.EditField.text;
      var start = this.SegStart(k);
      var end = start + this.aSegs[k].locked;
      var lineStart = start == 0 || text.charAt(start - 1) == "\r";
      var p = start;
      while(p < end)
      {
         var lineEnd = text.indexOf("\r", p);
         if(lineEnd < 0 || lineEnd > end)
         {
            lineEnd = end;
         }
         var level = lineStart ? BookMenu.HeadingLevel(text, p) : 0;
         if(level > 0 && p + level + 1 < lineEnd)
         {
            this.StyleHeadingLine(p, lineEnd, level, this.EditField.getTextFormat(p + level + 1, p + level + 2).size);
         }
         p = lineEnd + 1;
         lineStart = true;
      }
   }

   // One heading line [p, lineEnd) of this level (0: none): its mark at size 1, the rest scaled from base.
   function StyleHeadingLine(p, lineEnd, level, base)
   {
      var mark = level + 1;
      if(level == 0 || p + mark > lineEnd)
      {
         return undefined;
      }
      var tiny = new TextFormat();
      tiny.size = 1;
      this.EditField.setTextFormat(p, p + mark, tiny);
      if(lineEnd > p + mark && base > 0)
      {
         var f = new TextFormat();
         f.size = Math.round(base * BookMenu.HEADING_SCALE[level]);
         this.EditField.setTextFormat(p + mark, lineEnd, f);
      }
   }

   // Body k's formats after an edit.  With the plugin's hint (SetEditMarked's font and size): its text in them, each
   // "\r\r" in the page's outer size, as a renderer that tags paragraphs gives them.  Its blood red, always.
   function FormatBreaks(k)
   {
      var start = this.BodyStart(k);
      var end = this.BodyEnd(k);
      if(end <= start)
      {
         return undefined;
      }
      if(this.oBreakFmt != undefined)
      {
         this.EditField.setTextFormat(start, end, this.oContentFmt);
         var text = this.EditField.text;
         var pos = start;
         while(true)
         {
            var found = text.indexOf("\r\r", pos);
            if(found < 0 || found + 2 > end)
            {
               break;
            }
            this.EditField.setTextFormat(found, found + 2, this.oBreakFmt);
            pos = found + 2;
         }
      }
      // Text written in blood stays red.
      var blood = this.aSegs[k].blood;
      if(blood != undefined && blood.length)
      {
         var red = new TextFormat();
         red.color = BookMenu.BLOOD_COLOR;
         var b = 0;
         while(b < blood.length)
         {
            this.EditField.setTextFormat(start + blood[b].s, start + blood[b].e, red);
            b++;
         }
      }
      this.StyleHeadings(k);
   }

   // ---- Blood ----

   // From the plugin: this session writes in blood (the player chose it, having no ink).
   function EditSetBlood(on)
   {
      this.bBlood = on == true;
   }

   // Ranges sorted, overlapping and touching ones joined, empty ones dropped.
   static function MergeRanges(ranges)
   {
      ranges.sort(function(a, b)
      {
         return a.s - b.s;
      });
      var out = [];
      var i = 0;
      while(i < ranges.length)
      {
         var r = ranges[i];
         if(r.e > r.s)
         {
            if(out.length && r.s <= out[out.length - 1].e)
            {
               out[out.length - 1].e = Math.max(out[out.length - 1].e, r.e);
            }
            else
            {
               out.push({s:r.s, e:r.e});
            }
         }
         i++;
      }
      return out;
   }

   // A body for the plugin: the text with the blood markers around each red range.
   static function MarkBlood(text, ranges)
   {
      if(ranges == undefined)
      {
         return text;
      }
      var i = ranges.length - 1;
      while(i >= 0)
      {
         var r = ranges[i];
         text = text.substring(0, r.s) + String.fromCharCode(BookMenu.BLOOD_OPEN) + text.substring(r.s, r.e)
            + String.fromCharCode(BookMenu.BLOOD_CLOSE) + text.substring(r.e);
         i--;
      }
      return text;
   }

   // n characters typed at p in body k: the ranges after them move; typed in blood they're red,
   // typed in ink inside a red range they split it.
   function BloodInsert(k, p, n)
   {
      var seg = this.aSegs[k];
      var old = seg.blood == undefined ? [] : seg.blood;
      var out = [];
      var i = 0;
      while(i < old.length)
      {
         var r = old[i];
         if(r.e <= p)
         {
            out.push({s:r.s, e:r.e});
         }
         else if(r.s >= p)
         {
            out.push({s:r.s + n, e:r.e + n});
         }
         else
         {
            out.push({s:r.s, e:p});
            out.push({s:p + n, e:r.e + n});
         }
         i++;
      }
      if(this.bBlood)
      {
         out.push({s:p, e:p + n});
      }
      seg.blood = BookMenu.MergeRanges(out);
   }

   // The character at p in body k deleted.
   function BloodDelete(k, p)
   {
      var seg = this.aSegs[k];
      if(seg.blood == undefined)
      {
         return undefined;
      }
      var out = [];
      var i = 0;
      while(i < seg.blood.length)
      {
         var r = seg.blood[i];
         if(r.e <= p)
         {
            out.push({s:r.s, e:r.e});
         }
         else if(r.s > p)
         {
            out.push({s:r.s - 1, e:r.e - 1});
         }
         else
         {
            out.push({s:r.s, e:r.e - 1});
         }
         i++;
      }
      seg.blood = BookMenu.MergeRanges(out);
   }

   function SegStart(k)
   {
      var pos = 0;
      var j = 0;
      while(j < k)
      {
         pos += this.aSegs[j].locked + this.aSegs[j].body;
         j++;
      }
      return pos;
   }

   function BodyStart(k)
   {
      return this.SegStart(k) + this.aSegs[k].locked;
   }

   function BodyEnd(k)
   {
      return this.BodyStart(k) + this.aSegs[k].body;
   }

   // The editable segment whose body contains pos (either end included), or -1.
   function EditableSegAt(pos)
   {
      var k = 0;
      while(k < this.aSegs.length)
      {
         if(this.aSegs[k].editable && pos >= this.BodyStart(k) && pos <= this.BodyEnd(k))
         {
            return k;
         }
         k++;
      }
      return -1;
   }

   // The nearest caret position in a body: pos itself if it is in one, else the nearest body
   // end before it (dir < 0) or body start after it (dir > 0), else the other way, else -1.
   function EditSnap(pos, dir)
   {
      if(this.EditableSegAt(pos) >= 0)
      {
         return pos;
      }
      var before = -1;
      var after = -1;
      var k = 0;
      while(k < this.aSegs.length)
      {
         if(this.aSegs[k].editable)
         {
            if(this.BodyEnd(k) <= pos)
            {
               before = this.BodyEnd(k);
            }
            else if(after < 0 && this.BodyStart(k) >= pos)
            {
               after = this.BodyStart(k);
            }
         }
         k++;
      }
      if(dir < 0)
      {
         return before >= 0 ? before : after;
      }
      return after >= 0 ? after : before;
   }

   // The book's text has arrived (SetBookText, which comes after the menu opens): edit mode
   // can start.
   function EditReady()
   {
      return this.bTextReceived == true;
   }

   // Put the caret at the end of entry i's text and show it.
   function EditFocusEntry(i)
   {
      var entry = -1;
      var k = 0;
      while(k < this.aSegs.length)
      {
         if(this.aSegs[k].editable && ++entry == i)
         {
            this.EditSetCaret(this.BodyEnd(k));
            this.EditLayout();
            return true;
         }
         k++;
      }
      return false;
   }

   // The entry the caret is in (its index among the entries, as EditGetBodies orders them),
   // or -1.
   function EditCurrentEntry()
   {
      if(this.aSegs == undefined || this.EditField == undefined)
      {
         return -1;
      }
      var k = this.EditableSegAt(this.EditCaret());
      if(k < 0)
      {
         return -1;
      }
      var entry = 0;
      var j = 0;
      while(j < k)
      {
         if(this.aSegs[j].editable)
         {
            entry++;
         }
         j++;
      }
      return entry;
   }

   // The caret's place in its run (characters from the run's start, as EditReload's caretOffset), or -1.
   function EditCaretOffset()
   {
      if(this.aSegs == undefined || this.EditField == undefined)
      {
         return -1;
      }
      var pos = this.EditCaret();
      var k = this.EditableSegAt(pos);
      return k < 0 ? -1 : pos - this.BodyStart(k);
   }

   // The caret at pos, or none at all when there is no entry to type in (pos -1): a caret on
   // the blank or title page would look editable.
   function EditSetCaretOrNone(pos)
   {
      if(pos < 0)
      {
         // Unfocusing alone still draws the caret: a field that can't be typed in draws none.
         Selection.setFocus(null);
         this.EditField.type = "dynamic";
         this.EditField.selectable = false;
      }
      else
      {
         this.EditSetCaret(pos);
      }
   }

   // Leave edit mode and read again on the spread being edited, with the text rendered from the saved entries (none: the text the book had).
   // Lays it out as the engine's SetBookText does, keeping the engine's page slots where they are.
   function ReturnToReading(text)
   {
      if(text == undefined || !text.length)
      {
         // Nothing was saved: the text the book had.
         text = this.sBookText;
      }
      var page = this.iEditPage;
      var left = this.bNote ? page : page - page % 2;
      var setIndex = this.bNote ? page : left - this.iEditShownFrom;
      this.ExitEditMode();
      if(this.iPaginationIndex != -1)
      {
         clearInterval(this.iPaginationIndex);
         this.iPaginationIndex = -1;
      }
      while(this.BookPages.length)
      {
         this.BookPages.pop().removeMovieClip();
      }
      this.PageInfoA = new Array();
      this.SetBookText(text, this.bNote);
      this.iLeftPageNumber = left;
      this.iPageSetIndex = setIndex;
      this.UpdatePages();
   }

   // From the plugin, while reading (2): the open book is now another one (a blank journal became
   // a journal). Its text, from the first page.
   function ReplaceBookText(text)
   {
      if(this.bEditMode)
      {
         return undefined;
      }
      if(this.iPaginationIndex != -1)
      {
         clearInterval(this.iPaginationIndex);
         this.iPaginationIndex = -1;
      }
      while(this.BookPages.length)
      {
         this.BookPages.pop().removeMovieClip();
      }
      this.PageInfoA = new Array();
      this.SetBookText(text, this.bNote);
      this.iLeftPageNumber = 0;
      this.iPageSetIndex = 0;
      this.UpdatePages();
   }

   // Each body's text, in segment order, joined by \x1E (line breaks as \n). Blank page: one body.
   // Undefined once the editor is gone (PrepForClose): there is no text, not an empty one.
   function EditGetBodies()
   {
      if(this.aSegs == undefined || this.EditField == undefined)
      {
         return undefined;
      }
      var out = [];
      var k = 0;
      while(k < this.aSegs.length)
      {
         if(this.aSegs[k].editable)
         {
            var body = this.EditField.text.substring(this.BodyStart(k), this.BodyEnd(k));
            out.push(BookMenu.MarkBlood(body, this.aSegs[k].blood).split("\r").join("\n"));
         }
         k++;
      }
      return out.join(String.fromCharCode(30));
   }

   // ---- Pages ----

   // Recompute the page tops and show the caret's page. Call after every edit or caret move.
   function EditLayout()
   {
      if(this.EditField == undefined)
      {
         return undefined;
      }
      this.EditField.scroll = 1;
      var tf = this.EditField;
      var tops = [2];
      var bottoms = [];
      var firstLines = [0];
      var y = 2;   // Flash's text gutter: line 0 starts 2px down
      var caretLine = this.EditCaretLine();
      var caretPage = 0;
      // Where the blanked [pagebreak]s are (EditBuildMarked).
      var breakAt = {};
      var bk = 0;
      while(bk < this.aSegs.length)
      {
         var b = 0;
         while(this.aSegs[bk].breaks != undefined && b < this.aSegs[bk].breaks.length)
         {
            breakAt[this.SegStart(bk) + this.aSegs[bk].breaks[b]] = true;
            b++;
         }
         bk++;
      }
      var i = 0;
      while(i < tf.numLines)
      {
         var m = tf.getLineMetrics(i);
         var off = tf.getLineOffset(i);
         // As CalculatePagination: a [pagebreak] line ends the page above it, and the next starts below it.
         var lineEnd = i + 1 < tf.numLines ? tf.getLineOffset(i + 1) : tf.length;
         if(breakAt[off] || Shared.GlobalFunc.StringTrim(tf.text.substring(off, lineEnd)) == BookMenu.PAGE_BREAK_TAG)
         {
            bottoms[tops.length - 1] = y;
            y += m.height;
            tops.push(y);
            firstLines.push(i + 1);
            i++;
            continue;
         }
         // Same rule as CalculatePagination: a line whose bottom passes the page starts a new page.
         if(i > 0 && y + m.ascent + m.descent > tops[tops.length - 1] + this.iMaxPageHeight)
         {
            bottoms[tops.length - 1] = y;
            tops.push(y);
            firstLines.push(i);
         }
         if(i == caretLine)
         {
            caretPage = tops.length - 1;
         }
         y += m.height;
         i++;
      }
      tops.push(y);   // end of the text: bottom of the last page
      this.aEditPageTops = tops;
      this.aEditPageBottoms = bottoms;
      this.aEditPageLines = firstLines;
      this.iEditPage = caretPage;
      this.ShowEditPage(caretPage);
   }

   function EditPageCount()
   {
      return this.aEditPageTops.length - 1;
   }

   function EditCaretLine()
   {
      var pos = Selection.getBeginIndex();
      var tf = this.EditField;
      if(pos < 0 || pos >= tf.length)
      {
         return tf.numLines - 1;
      }
      return tf.getLineIndexOfChar(pos);
   }

   // Move the field so page p sits where page 0 would, and mask everything else.
   function ShowEditPage(p)
   {
      var last = this.EditPageCount() - 1;
      if(p > last)
      {
         p = last;
      }
      if(p < 0)
      {
         p = 0;
      }
      var top = this.aEditPageTops[p];
      var bottom = p < last ? this.aEditPageTops[p + 1] : top + this.iMaxPageHeight;
      if(p < last && this.aEditPageBottoms[p] != undefined)
      {
         bottom = this.aEditPageBottoms[p];
      }
      this.EditField._y = 2 - top;
      this.EditClip._visible = true;
      var m = this.EditMask;
      m.clear();
      m._x = this.EditClip._x;
      m._y = this.EditClip._y;
      m.beginFill(0xFF0000, 100);
      // Wider than the field: handwritten glyphs overhang its edges.
      var left = - BookMenu.EDIT_MASK_OVERHANG;
      var right = this.EditField._width + BookMenu.EDIT_MASK_OVERHANG;
      var h = bottom - top + 2;
      m.moveTo(left, 0);
      m.lineTo(right, 0);
      m.lineTo(right, h);
      m.lineTo(left, h);
      m.lineTo(left, 0);
      m.endFill();
   }

   // Put the caret on page p (its first character) and show it.
   function EditGoToPage(p)
   {
      this.EditSuggestClear();
      if(p < 0 || p >= this.EditPageCount())
      {
         return false;
      }
      var pos = this.EditField.getLineOffset(this.aEditPageLines[p]);
      if(pos < 0)
      {
         pos = this.EditField.length;
      }
      pos = this.EditSnap(pos, 1);
      var landed = pos < 0 ? -1 : this.PageOfPos(pos);
      // A book's spread shows p and p + 1; a note one page.
      if(landed == p || !this.bNote && landed == p + 1)
      {
         this.EditSetCaret(pos);
         this.EditLayout();
      }
      else
      {
         // Nowhere to type on that page (the blank or title page): show it, keep the caret.
         this.iEditPage = p;
         this.ShowEditPage(p);
      }
      return true;
   }

   function PageOfPos(pos)
   {
      var tf = this.EditField;
      var line = pos >= tf.length ? tf.numLines - 1 : tf.getLineIndexOfChar(pos);
      var p = 0;
      while(p + 1 < this.aEditPageLines.length && this.aEditPageLines[p + 1] <= line)
      {
         p++;
      }
      return p;
   }


   // The quill cursor's point: "side,page,x,y,gx,gy". side: 0 the left page (or a note), 1 the right; x, y: the caret's
   // left edge and its line's bottom, in the field's units from the top of its page; gx, gy: that point on the stage.
   function EditCaretPoint()
   {
      var tf = this.EditField;
      var pos = this.EditCaret();
      var page = this.PageOfPos(pos);
      var line = pos >= tf.length ? tf.numLines - 1 : tf.getLineIndexOfChar(pos);
      var r = pos < tf.length ? tf.getCharBoundaries(pos) : undefined;
      var x = 2;
      if(r != undefined)
      {
         x = r.x;
      }
      else if(pos > 0 && pos != tf.getLineOffset(line))
      {
         var prev = tf.getCharBoundaries(pos - 1);
         x = prev == undefined ? 2 : prev.x + prev.width;
      }
      var y = 2;
      var i = this.aEditPageLines[page];
      while(i <= line)
      {
         y += tf.getLineMetrics(i).height;
         i++;
      }
      var side = this.bNote ? 0 : page - this.iEditPage;
      // ShowEditPage puts a page's top at the clip's top (the field's _y = 2 - that top), so y is the clip's y.
      var pt = {x:x + tf._x, y:y};
      this.EditClip.localToGlobal(pt);
      return side + "," + page + "," + x + "," + y + "," + pt.x + "," + pt.y;
   }

   // ---- Suggestions (inline completion, docs/EDITOR.md#suggestions) ----

   // From the plugin (5): candidates for the text at the caret, separated by U+001F.
   function EditSuggest(list)
   {
      this.EditSuggestClear();
      if(this.EditField == undefined || list == undefined || !list.length)
      {
         return false;
      }
      this.aSuggest = list.split(String.fromCharCode(31));
      this.iSuggest = 0;
      this.iSuggestAt = this.EditCaret();
      this.ShowSuggestion();
      return true;
   }

   // A suggestion shows, for where the caret still is.
   function EditSuggesting()
   {
      return this.aSuggest != undefined && this.EditField != undefined && this.EditCaret() == this.iSuggestAt;
   }

   function EditSuggestNext(step)
   {
      if(!this.EditSuggesting())
      {
         return undefined;
      }
      var n = this.aSuggest.length;
      this.iSuggest = ((this.iSuggest + Number(step)) % n + n) % n;
      this.ShowSuggestion();
   }

   // The suggestion shown, cleared: the plugin types it.  "" if none.
   function EditSuggestTake()
   {
      var text = this.EditSuggesting() ? this.aSuggest[this.iSuggest] : "";
      this.EditSuggestClear();
      return text;
   }

   function EditSuggestClear()
   {
      this.aSuggest = undefined;
      if(this.SuggestField != undefined)
      {
         this.SuggestField.removeTextField();
         this.SuggestField = undefined;
      }
   }

   // Its own field on the edit clip (under the same mask), at the caret, in the caret's font and size: never in
   // the edited text, so runs, layout and the format typed text takes are untouched.
   function ShowSuggestion()
   {
      var tf = this.EditField;
      var pos = this.EditCaret();
      var line = pos >= tf.length ? tf.numLines - 1 : tf.getLineIndexOfChar(pos);
      var ch = pos < tf.length ? tf.text.charAt(pos) : "";
      var r = ch.length && ch != "\r" && ch != "\n" ? tf.getCharBoundaries(pos) : undefined;
      var x = 2;
      if(r != undefined)
      {
         x = r.x;
      }
      else if(pos > 0 && pos != tf.getLineOffset(line))
      {
         var prev = tf.getCharBoundaries(pos - 1);
         x = prev == undefined ? 2 : prev.x + prev.width;
      }
      var top = 2;
      var i = 0;
      while(i < line)
      {
         top += tf.getLineMetrics(i).height;
         i++;
      }
      var fmt = pos > 0 ? tf.getTextFormat(pos - 1, pos) : tf.getNewTextFormat();
      if(this.SuggestField == undefined)
      {
         this.EditClip.createTextField("SuggestField", this.EditClip.getNextHighestDepth(), 0, 0, 10, 10);
         this.SuggestField = this.EditClip.SuggestField;
         this.SuggestField.embedFonts = true;
         this.SuggestField.selectable = false;
         this.SuggestField.multiline = false;
         this.SuggestField.wordWrap = false;
         this.SuggestField.autoSize = "left";
      }
      var s = this.SuggestField;
      var text = this.aSuggest[this.iSuggest];
      var f = new TextFormat();
      f.font = fmt.font;
      f.size = fmt.size;
      f.color = BookMenu.SUGGEST_COLOR;
      f.kerning = fmt.kerning;
      f.letterSpacing = fmt.letterSpacing;
      // Cut to the field's right edge with "...".
      var room = tf._width - x;
      var shown = text;
      while(true)
      {
         s.text = shown;
         s.setTextFormat(f);
         if(s.textWidth <= room || text.length <= 1)
         {
            break;
         }
         text = text.substr(0, text.length - 1);
         shown = text + "...";
      }
      s._x = tf._x + x - 2;
      s._y = tf._y + top - 2;
   }

   function EditCaret()
   {
      var pos = Selection.getBeginIndex();
      return pos < 0 ? this.EditField.length : pos;
   }

   function EditSetCaret(pos)
   {
      this.EditField.type = "input";
      this.EditField.selectable = true;
      Selection.setFocus(this.EditField);
      Selection.setSelection(pos, pos);
   }

   // Typing goes into the body at the caret; the caret is only ever in a body.
   function AppendEditChar(ch)
   {
      if(this.EditField == undefined)
      {
         return undefined;
      }
      if(ch == "\n")
      {
         ch = "\r";
      }
      var pos = this.EditSnap(this.EditCaret(), 1);
      var k = this.EditableSegAt(pos);
      if(k < 0)
      {
         return undefined;
      }
      // The format of the text it's typed into (the hint's in an empty run).
      var fmt = this.EditField.getNewTextFormat();
      if(pos > this.BodyStart(k))
      {
         fmt = this.EditField.getTextFormat(pos - 1, pos);
      }
      else if(pos < this.BodyEnd(k))
      {
         fmt = this.EditField.getTextFormat(pos, pos + 1);
      }
      else if(this.oContentFmt != undefined)
      {
         fmt = this.oContentFmt;
      }
      this.EditField.replaceText(pos, pos, ch);
      this.EditField.setTextFormat(pos, pos + ch.length, fmt);
      this.BloodInsert(k, pos - this.BodyStart(k), ch.length);
      this.aSegs[k].body += ch.length;
      this.FormatBreaks(k);
      this.EditSetCaret(pos + ch.length);
      this.EditLayout();
   }

   // Whether EditBackspace (forward false) or EditDelete (true) would remove a character: the
   // plugin charges ink or blood only for a key that changes the text.
   function EditCanErase(forward)
   {
      if(this.EditField == undefined)
      {
         return false;
      }
      var pos = this.EditCaret();
      var k = this.EditableSegAt(pos);
      if(k < 0)
      {
         return false;
      }
      return forward ? pos < this.BodyEnd(k) : pos > this.BodyStart(k);
   }

   // Stops at the start of the body: headings and the title page can't be deleted.
   function EditBackspace()
   {
      if(this.EditField == undefined)
      {
         return undefined;
      }
      var pos = this.EditCaret();
      var k = this.EditableSegAt(pos);
      if(k >= 0 && pos > this.BodyStart(k))
      {
         this.EditField.replaceText(pos - 1, pos, "");
         this.BloodDelete(k, pos - 1 - this.BodyStart(k));
         this.aSegs[k].body -= 1;
         this.FormatBreaks(k);
         this.EditSetCaret(pos - 1);
      }
      this.EditLayout();
   }

   function EditDelete()
   {
      if(this.EditField == undefined)
      {
         return undefined;
      }
      var pos = this.EditCaret();
      var k = this.EditableSegAt(pos);
      if(k >= 0 && pos < this.BodyEnd(k))
      {
         this.EditField.replaceText(pos, pos + 1, "");
         this.BloodDelete(k, pos - this.BodyStart(k));
         this.aSegs[k].body -= 1;
         this.FormatBreaks(k);
         this.EditSetCaret(pos);
      }
      this.EditLayout();
   }

   // Moves like a text box, then out of any locked text in the direction of travel.
   function EditMoveCursor(direction)
   {
      if(this.EditField == undefined)
      {
         return undefined;
      }
      var tf = this.EditField;
      var pos = this.EditCaret();
      var len = tf.length;
      var target = pos;
      var dir = 1;
      if(direction == "left")
      {
         target = pos - 1;
         dir = -1;
      }
      else if(direction == "right")
      {
         target = pos + 1;
      }
      else if(direction == "home")
      {
         target = 0;
      }
      else if(direction == "end")
      {
         target = len;
         dir = -1;
      }
      else if(direction == "up" || direction == "down")
      {
         var line = pos >= len ? tf.numLines - 1 : tf.getLineIndexOfChar(pos);
         var other = direction == "up" ? line - 1 : line + 1;
         dir = direction == "up" ? -1 : 1;
         if(other >= 0 && other < tf.numLines)
         {
            var col = pos - tf.getLineOffset(line);
            var otherStart = tf.getLineOffset(other);
            var otherEnd = other + 1 < tf.numLines ? tf.getLineOffset(other + 1) - 1 : len;
            target = Math.min(otherStart + col, otherEnd);
         }
      }
      if(target < 0 || target > len)
      {
         target = pos;
      }
      var snapped = this.EditSnap(target, dir);
      this.EditSetCaret(snapped < 0 ? pos : snapped);
      this.EditLayout();
   }

   function ExitEditMode()
   {
      this.EditSuggestClear();
      if(this.EditClip != undefined)
      {
         this.EditClip.setMask(null);
         this.EditClip.removeMovieClip();
         this.EditClip = undefined;
         this.EditField = undefined;
      }
      if(this.EditMask != undefined)
      {
         this.EditMask.removeMovieClip();
         this.EditMask = undefined;
      }
      this.bEditMode = false;
      this.iEditPage = 0;
      this.oEditMarked = undefined;
      this.aSegs = undefined;
   }

   // Override TurnPage to handle edit mode page flipping
   function TurnPage(aiDelta)
   {
      if(this.bEditMode)
      {
         return this.EditTurnPage(aiDelta);
      }
      // ---- Original TurnPage logic ----
      var _loc2_ = this.iLeftPageNumber + aiDelta;
      var _loc4_ = _loc2_ >= 0 && _loc2_ < this.PageInfoA.length;
      if(this.bNote)
      {
         _loc4_ = _loc2_ >= 0 && _loc2_ < this.PageInfoA.length - 1;
      }
      var _loc3_ = Math.abs(aiDelta);
      var _loc5_;
      if(_loc4_)
      {
         _loc5_ = _loc3_ != 1 ? 4 : 1;
         this.SetLeftPageNumber(_loc2_);
         if(this.iLeftPageNumber < this.iPageSetIndex)
         {
            this.iPageSetIndex -= _loc3_;
         }
         else if(this.iLeftPageNumber >= this.iPageSetIndex + _loc5_)
         {
            this.iPageSetIndex += _loc3_;
         }
         this.UpdatePages();
      }
      return _loc4_;
   }

   // The engine's page turn (a click; the plugin keeps the arrow keys from it). Books turn a
   // spread, notes a page. Only to a page that exists: false skips the turn animation.
   function EditTurnPage(aiDelta)
   {
      if(getTimer() < this.iSuppressTurnUntil)
      {
         return false;
      }
      var from = this.bNote ? this.iEditPage : this.EditSpreadLeft();
      var ok = this.EditGoToPage(from + aiDelta);
      if(ok && !this.bNote)
      {
         // Vanilla TurnPage's page window: after a forward turn the engine shows the spread in
         // slots 2-3 (the turned leaf's far side), after a backward turn in slots 0-1.
         this.iEditShownFrom = aiDelta > 0 ? Math.abs(aiDelta) : 0;
      }
      return ok;
   }

   // Called on every key event while editing (press, repeat, release): the page keys (arrows, A, D) type or move the
   // caret, so a page turn while keys are in use is theirs, not a click's.
   function EditSuppressTurn()
   {
      this.iSuppressTurnUntil = getTimer() + BookMenu.EDIT_KEY_TURN_BLOCK_MS;
   }

   // Left page of the spread the caret is on (books show pages in pairs).
   function EditSpreadLeft()
   {
      return this.iEditPage - this.iEditPage % 2;
   }

   // Override PrepForClose to handle edit mode cleanup
   function PrepForClose()
   {
      if(this.bEditMode)
      {
         this.ExitEditMode();
         return undefined;
      }
      this.iPageSetIndex = this.iLeftPageNumber;
   }

   // ======== Vanilla's methods (Ink & Quill's changes in them; Convenient Reading's in //@variant blocks) ========

   //@variant convenient-reading
   static function trim(str)
   {
      var _loc2_ = 0;
      var _loc1_ = str.length - 1;
      while(str.charCodeAt(_loc2_) < 33)
      {
         _loc2_ = _loc2_ + 1;
      }
      while(str.charCodeAt(_loc1_) < 33)
      {
         _loc1_ = _loc1_ - 1;
      }
      return str.substring(_loc2_,_loc1_ + 1);
   }

   static function ParseConfig(str, par)
   {
      var _loc3_ = str.split("\n");
      var _loc4_ = 0;
      var _loc5_ = 0;
      var _loc6_;
      var _loc7_;
      var _loc8_;
      var _loc9_;
      while(_loc4_ < _loc3_.length)
      {
         if(_loc3_[_loc4_].charAt(0) != "#" && _loc3_[_loc4_].charAt(0) != "[")
         {
            _loc6_ = BookMenu.trim(_loc3_[_loc4_]);
            _loc7_ = _loc6_.indexOf("=");
            _loc8_ = _loc6_.substring(0,_loc7_);
            _loc9_ = BookMenu.trim(_loc8_);
            if(_loc9_ == par)
            {
               _loc5_ = _loc4_;
               break;
            }
         }
         _loc4_ += 1;
      }
      var _loc10_ = BookMenu.trim(_loc3_[_loc5_]);
      var _loc11_ = _loc10_.indexOf("=");
      var _loc12_ = _loc10_.substring(_loc11_ + 1,_loc10_.length);
      return BookMenu.trim(_loc12_);
   }
   //@end

   function SetBookText(astrText, abNote)
   {
      this.bNote = abNote;
      this.bTextReceived = true;
      // Don't overwrite text while in edit mode
      if(this.bEditMode)
      {
         return;
      }
      this.sBookText = astrText;
      this.ReferenceTextField.verticalAutoSize = "top";
      this.ReferenceTextField.SetText(this.PageHtml(astrText),true);
      if(abNote)
      {
         this.ReferenceTextField._width = BookMenu.NOTE_WIDTH;
      }
      BookMenu.ReadHeadings(this.ReferenceTextField);
      this.PageInfoA.push({pageTop:0,pageHeight:this.iMaxPageHeight});
      this.iCurrentLine = 0;
      this.iPaginationIndex = setInterval(this,"CalculatePagination",30);
      this.iNextPageBreak = this.iMaxPageHeight;
      this.SetLeftPageNumber(0);
   }

   function CreateDisplayPage(PageTop, PageBottom, aPageNum)
   {
      var _loc2_ = this.ReferenceText_mc.duplicateMovieClip("Page",this.getNextHighestDepth());
      var _loc3_ = _loc2_.PageTextField;
      _loc3_.noTranslate = true;
      _loc3_.SetText(this.ReferenceTextField.htmlText,true);
      var _loc4_ = this.ReferenceTextField.getLineOffset(this.ReferenceTextField.getLineIndexAtPoint(0,PageTop));
      var _loc5_ = this.ReferenceTextField.getLineOffset(this.ReferenceTextField.getLineIndexAtPoint(0,PageBottom));
      _loc3_.replaceText(0,_loc4_,"");
      _loc3_.replaceText(_loc5_ - _loc4_,this.ReferenceTextField.length,"");
      _loc3_.autoSize = "left";
      if(this.bNote)
      {
         _loc3_._width = BookMenu.NOTE_WIDTH;
         _loc2_._x = Stage.visibleRect.x + BookMenu.NOTE_X_OFFSET;
         _loc2_._y = Stage.visibleRect.y + BookMenu.NOTE_Y_OFFSET;
      }
      else
      {
         _loc2_._x = this.ReferenceText_mc._x;
         _loc2_._y = this.ReferenceText_mc._y;
      }
      _loc2_._visible = false;
      _loc2_.pageNum = aPageNum;
      this.BookPages.push(_loc2_);
   }

   function CalculatePagination()
   {
      var _loc7_ = false;
      var _loc5_;
      var _loc6_;
      var _loc3_;
      var _loc4_;
      var _loc2_;
      //@variant vanilla
      while(!_loc7_ && this.iCurrentLine <= this.ReferenceTextField.numLines)
      //@end
      //@variant convenient-reading
      while(this.iCurrentLine <= this.ReferenceTextField.numLines)
      //@end
      {
         _loc5_ = this.ReferenceTextField.getLineOffset(this.iCurrentLine);
         _loc6_ = this.ReferenceTextField.getLineOffset(this.iCurrentLine + 1);
         _loc3_ = this.ReferenceTextField.getCharBoundaries(_loc5_);
         _loc4_ = _loc6_ == -1 ? this.ReferenceTextField.text.substring(_loc5_) : this.ReferenceTextField.text.substring(_loc5_,_loc6_);
         _loc4_ = Shared.GlobalFunc.StringTrim(_loc4_);
         if(_loc3_.bottom > this.iNextPageBreak || _loc4_ == BookMenu.PAGE_BREAK_TAG || this.iCurrentLine >= this.ReferenceTextField.numLines)
         {
            _loc2_ = {pageTop:0,pageHeight:this.iMaxPageHeight};
            if(_loc4_ == BookMenu.PAGE_BREAK_TAG)
            {
               _loc2_.pageTop = _loc3_.bottom + this.ReferenceTextField.getLineMetrics(this.iCurrentLine).leading;
               this.PageInfoA[this.PageInfoA.length - 1].pageHeight = _loc3_.top - this.PageInfoA[this.PageInfoA.length - 1].pageTop;
            }
            else
            {
               _loc2_.pageTop = _loc3_.top;
               this.PageInfoA[this.PageInfoA.length - 1].pageHeight = _loc2_.pageTop - this.PageInfoA[this.PageInfoA.length - 1].pageTop;
            }
            this.iNextPageBreak = _loc2_.pageTop + this.iMaxPageHeight;
            if(_loc2_.pageTop != undefined || this.bNote)
            {
               this.PageInfoA.push(_loc2_);
            }
            _loc7_ = true;
         }
         this.iCurrentLine++;
      }
      if(this.iCurrentLine >= this.ReferenceTextField.numLines)
      {
         clearInterval(this.iPaginationIndex);
         this.iPaginationIndex = -1;
      }
      this.UpdatePages();
   }

   function SetLeftPageNumber(aiPageNum)
   {
      if(aiPageNum < this.PageInfoA.length)
      {
         this.iLeftPageNumber = aiPageNum;
      }
   }

   function ShowPageAtOffset(aiPageOffset)
   {
      if(this.bEditMode && this.EditField != undefined)
      {
         // The engine draws each side of the open book by calling this with 0, then 1.
         // Books: the engine's slots 0-3 are a window of pages; the current spread sits at
         // slots iEditShownFrom and +1, the other two are the far side of a turning leaf.
         var p = this.bNote ? this.iEditPage : this.EditSpreadLeft() - this.iEditShownFrom + aiPageOffset;
         this.EditClip._visible = p >= 0 && p < this.EditPageCount();
         if(this.EditClip._visible)
         {
            this.ShowEditPage(p);
         }
         return undefined;
      }
      var _loc2_ = 0;
      while(_loc2_ < this.BookPages.length)
      {
         if(this.BookPages[_loc2_].pageNum == this.iPageSetIndex + aiPageOffset)
         {
            this.BookPages[_loc2_]._visible = true;
         }
         else
         {
            this.BookPages[_loc2_]._visible = false;
         }
         _loc2_ = _loc2_ + 1;
      }
   }

   function UpdatePages()
   {
      var _loc2_ = 0;
      var _loc4_;
      var _loc3_;
      while(_loc2_ < BookMenu.CACHED_PAGES)
      {
         _loc4_ = false;
         _loc3_ = 0;
         while(!_loc4_ && _loc3_ < this.BookPages.length)
         {
            if(this.BookPages[_loc3_].pageNum == this.iPageSetIndex + _loc2_)
            {
               _loc4_ = true;
            }
            _loc3_ = _loc3_ + 1;
         }
         if(!_loc4_ && (this.PageInfoA.length > this.iPageSetIndex + _loc2_ + 1 || this.iPaginationIndex == -1 && this.PageInfoA.length > this.iPageSetIndex + _loc2_))
         {
            this.CreateDisplayPage(this.PageInfoA[this.iPageSetIndex + _loc2_].pageTop,this.PageInfoA[this.iPageSetIndex + _loc2_].pageTop + this.PageInfoA[this.iPageSetIndex + _loc2_].pageHeight,this.iPageSetIndex + _loc2_);
         }
         _loc2_ = _loc2_ + 1;
      }
      var _loc5_ = 0;
      while(_loc5_ < this.BookPages.length)
      {
         if(this.BookPages[_loc5_].pageNum < this.iPageSetIndex || this.BookPages[_loc5_].pageNum >= this.iPageSetIndex + BookMenu.CACHED_PAGES)
         {
            this.BookPages.splice(_loc5_,1)[0].removeMovieClip();
         }
         _loc5_ = _loc5_ + 1;
      }
   }
}
