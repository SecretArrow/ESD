#pragma once

#include <QColor>
#include <QMutex>
#include <QObject>
#include <QString>
#include <deque>
#include <functional>

#include <vterm.h>

namespace eclipse {

// ---------------------------------------------------------------------------
// VtEmulator - libvterm wrapper exposing a paint-friendly snapshot API.
//
//  * The QML TermItem reads cells directly via cellAt()/scrollbackLine().
//  * Output bytes (keyboard/mouse) go through outputCallback -> pty channel.
//  * Damage notifications coalesce into repaint requests.
// ---------------------------------------------------------------------------
class VtEmulator : public QObject
{
    Q_OBJECT
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

signals:
    void damage();
    void outputReady(const QByteArray& bytes);
    void bell();
    void titleChanged(const QString& title);
    void emulatorResized(int cols, int rows);

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

    mutable QMutex m_mutex;

    static const VTermScreenCallbacks s_screenCallbacks;
};

} // namespace eclipse
