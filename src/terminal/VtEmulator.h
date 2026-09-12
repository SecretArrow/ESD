#pragma once

#include <QColor>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

#include <vterm.h>

namespace eclipse {

// ---------------------------------------------------------------------------
// VtEmulator - libvterm wrapper exposing a paint-friendly snapshot API.
//
//  * The QML TermItem reads cells directly via cellAt()/scrollbackLine().
//  * Output bytes (keyboard/mouse) go through outputCallback -> pty channel.
//  * Damage notifications coalesce into repaint requests.
//
// Task 2-a additions:
//  * find-in-buffer: find()/rowText() over scrollback + active screen using
//    absolute row addressing (0 = oldest scrollback row).
//  * OSC 133 shell integration: a byte-stream scanner in feed() extracts
//    "ESC ] 133 ; {A,B,C,D[;exit]} ST/BEL" markers (all bytes are still fed
//    to libvterm, which ignores unknown OSC). Marks are stored per buffer row
//    (parallel deques/vectors), so they scroll with the content.
//  * Sixel graphics: the same scanner captures DCS "q" payloads and decodes
//    them through SixelDecoder, emitting sixelImageReady(). libvterm 0.3.3 has
//    no DCS hook and silently consumes DCS strings, so the raw bytes are
//    still forwarded (nothing is drawn by libvterm for them).
// ---------------------------------------------------------------------------
class VtEmulator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool shellIntegrationActive READ shellIntegrationActive NOTIFY shellIntegrationChanged)

public:
    struct Cell
    {
        QString text;      // UTF-8 decoded grapheme (usually 1 char)
        int width = 1;     // 2 = wide (CJK)
        bool bold = false;
        bool underline = false;
        bool italic = false;
        bool reverse = false;
        bool blink = false;
        bool strike = false;
        QColor fg;
        QColor bg;
    };

    // Per-row shell-integration marker (OSC 133). Stored on buffer row
    // identity so markers travel with the content into/within scrollback.
    enum class RowMark : quint8
    {
        None,
        PromptStart,   // 133;A
        OutputStart,   // 133;B
        CommandStart,  // 133;C
        CommandDone    // 133;D[;exit]
    };

    explicit VtEmulator(int cols = 80, int rows = 24, QObject* parent = nullptr);
    ~VtEmulator() override;

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }

    // Feed raw PTY output into the emulator.
    void feed(const char* data, size_t len);

    // Keyboard/mouse in
    void inputBytes(const QByteArray& bytes);
    void keyPress(int vtermKey, int mods);
    void unicodePress(uint codepoint, int mods);
    void mouseMove(int row, int col, int mods);
    void mouseButton(int button, bool pressed, int mods, int row, int col);
    void pasteText(const QString& text);
    bool mouseReportingEnabled() const { return m_mouseMode > 0; }

    // Resize with reflow (libvterm handles reflow when enabled)
    void resize(int cols, int rows);

    // Reading state
    Cell cellAt(int row, int col) const;
    int cursorRow() const { return m_cursorRow; }
    int cursorCol() const { return m_cursorCol; }
    bool cursorVisible() const { return m_cursorVisible; }

    // Scrollback
    int scrollbackCount() const;
    Cell scrollbackCell(int lineIndex, int col) const; // lineIndex: 0 = oldest
    void clearScrollback();

    // Palette (16 colors) for indexed colors
    void setPaletteColor(int index, const QColor& c);
    QColor indexedColor(int index) const;

    // Title set by remote (OSC)
    QString terminalTitle() const { return m_title; }

    // ---- find-in-buffer (Task 2-a) -----------------------------------------
    // Absolute row addressing: [0, scrollbackCount()) = scrollback rows
    // (0 = oldest), [scrollbackCount(), scrollbackCount() + rows()) = the
    // active screen. rowText() returns the row's text (one char per cell);
    // find() returns (absoluteRow, cellCol) for every match, ordered.
    QString rowText(int absoluteRow) const;
    QVector<QPair<int, int>> find(const QString& needle, bool caseSensitive) const;

    // ---- OSC 133 shell integration (Task 2-a) ------------------------------
    RowMark rowMark(int absoluteRow) const;
    Q_INVOKABLE int previousPromptRow(int currentViewportTop) const; // absolute row or -1
    Q_INVOKABLE int nextPromptRow(int currentViewportTop) const;     // absolute row or -1
    bool shellIntegrationActive() const;

signals:
    void damage();
    void outputReady(const QByteArray& bytes);
    void bell();
    void titleChanged(const QString& title);
    void emulatorResized(int cols, int rows);

    // Task 2-a signals
    void commandStarted();                 // OSC 133;C
    void commandFinished(int exitCode);    // OSC 133;D;<code> (only when a code was present)
    void shellIntegrationChanged();        // first A/C/D observed
    void sixelImageReady(const QImage& image);

private:
    static int cbDamage(VTermRect rect, void* user);
    static int cbMoveRect(VTermRect dest, VTermRect src, void* user);
    static int cbMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
    static int cbSetTermProp(VTermProp prop, VTermValue* val, void* user);
    static int cbBell(void* user);
    static int cbResize(int rows, int cols, void* user);
    static int cbPushLine(int cols, const VTermScreenCell* cells, void* user);
    static int cbPopLine(int cols, VTermScreenCell* cells, void* user);
    static int cbClear(void* user);
    static void cbOutput(const char* bytes, size_t len, void* user);

    void pushScrollbackLine(int cols, const VTermScreenCell* cells);
    bool popScrollbackLine(int cols, VTermScreenCell* cells);
    QColor resolveColor(const VTermColor& color, bool isForeground) const;

    // ---- byte-stream scanner (OSC 133 / sixel DCS) -------------------------
    struct ScanEvent
    {
        enum Type { PromptStart, OutputStart, CommandStart, CommandDone, SixelImage } type;
        int exitCode = -1;    // CommandDone: >= 0 when a code was present
        QImage image;         // SixelImage
        size_t endOffset = 0; // chunk offset just past the sequence
    };

    enum class ScanState : quint8
    {
        Ground,    // outside escape sequences
        Esc,       // ESC seen
        Osc,       // inside OSC payload
        OscEsc,    // ESC seen inside OSC (expects '\' for ST)
        DcsIntro,  // inside DCS introducer parameters (until final byte)
        DcsSixel,  // inside sixel payload
        DcsEsc     // ESC seen inside sixel payload
    };

    void scanByte(unsigned char b, size_t offset, std::vector<ScanEvent>& events);
    void completeOsc(size_t endOffset, std::vector<ScanEvent>& events);
    void finishSixel(size_t endOffset, std::vector<ScanEvent>& events);
    void applyScanEvent(const ScanEvent& ev);
    void setScreenMark(int row, RowMark mark);
    void moveScreenMarks(const VTermRect& dest, const VTermRect& src);

    // Row text helpers; the unlocked variant assumes m_mutex is held.
    // colMap (optional) receives the cell column for every text index.
    QString rowTextUnlocked(int absoluteRow, std::vector<int>* colMap) const;

    VTerm* m_vt = nullptr;
    VTermScreen* m_screen = nullptr;
    int m_cols = 80;
    int m_rows = 24;

    int m_cursorRow = 0;
    int m_cursorCol = 0;
    bool m_cursorVisible = true;
    int m_mouseMode = 0;
    QString m_title;
    QByteArray m_titleBuffer;

    QColor m_palette[258]; // 16 configurable + 240 static + 2 fallback slots
    std::deque<std::vector<Cell>> m_scrollback;
    int m_scrollbackLimit = 10000;

    // ---- scanner / shell-integration state --------------------------------
    ScanState m_scanState = ScanState::Ground;
    QByteArray m_oscBuffer;
    QByteArray m_dcsIntro;     // bytes between "ESC P" and the final byte
    QByteArray m_sixelBuffer;  // sixel payload under collection
    bool m_sixelTransparent = false;
    bool m_captureOverflow = false;

    std::vector<RowMark> m_screenMarks;    // per active-screen row
    std::deque<RowMark> m_scrollbackMarks; // aligned with m_scrollback
    bool m_shellIntegrationActive = false;

    // Restored-from-scrollback line (cbPopLine) may be positioned by a
    // subsequent moverect, so its mark is applied after the write completes.
    bool m_pendingPopMarkValid = false;
    RowMark m_pendingPopMark = RowMark::None;

    mutable QMutex m_mutex;

    static const VTermScreenCallbacks s_screenCallbacks;
};

} // namespace eclipse
