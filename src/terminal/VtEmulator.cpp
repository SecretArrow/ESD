#include "VtEmulator.h"

#include "SixelDecoder.h"

#include <QMutexLocker>

#include <algorithm>
#include <cstring>

#include <vterm.h>
#include <vterm_keycodes.h>

namespace eclipse {

const VTermScreenCallbacks VtEmulator::s_screenCallbacks = {
    cbDamage, cbMoveRect, cbMoveCursor, cbSetTermProp, cbBell, cbResize, cbPushLine, cbPopLine, cbClear,
};

namespace {

constexpr int kScrollDamageEveryN = 1;
constexpr size_t kMaxOscBytes = 64 * 1024;          // per OSC payload
constexpr size_t kMaxSixelBytes = 32 * 1024 * 1024; // per sixel payload

} // namespace

// ---------------------------------------------------------------------------
VtEmulator::VtEmulator(int cols, int rows, QObject* parent)
    : QObject(parent)
    , m_cols(cols)
    , m_rows(rows)
{
    // Base 16 palette (eclipse-dark scheme defaults)
    const QColor base[16] = {
        QColor(0x1e, 0x22, 0x2b), QColor(0xe2, 0x5d, 0x5d), QColor(0x3f, 0xb6, 0x6f),
        QColor(0xe2, 0xb1, 0x2c), QColor(0x5b, 0x8d, 0xef), QColor(0xb4, 0x6b, 0xd6),
        QColor(0x4f, 0xc2, 0xc5), QColor(0xc6, 0xcb, 0xd4), QColor(0x5a, 0x61, 0x70),
        QColor(0xef, 0x83, 0x83), QColor(0x69, 0xd1, 0x91), QColor(0xf0, 0xc6, 0x63),
        QColor(0x84, 0xac, 0xf7), QColor(0xd0, 0x93, 0xe6), QColor(0x7b, 0xd8, 0xdb),
        QColor(0xf2, 0xf4, 0xf8),
    };
    for (int i = 0; i < 16; ++i)
        m_palette[i] = base[i];
    for (int i = 16; i < 256; ++i) {
        // xterm 240-color cube/greys
        if (i < 232) {
            const int n = i - 16;
            const int r = n / 36, g = (n / 6) % 6, b = n % 6;
            const auto lv = [](int c) { return c == 0 ? 0 : 55 + c * 40; };
            m_palette[i] = QColor(lv(r), lv(g), lv(b));
        } else {
            const int v = 8 + (i - 232) * 10;
            m_palette[i] = QColor(v, v, v);
        }
    }
    m_palette[256] = QColor(0xe6, 0xe8, 0xee); // default fg
    m_palette[257] = QColor(0x14, 0x17, 0x1e); // default bg

    m_screenMarks.assign(size_t(m_rows), RowMark::None);

    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    vterm_output_set_callback(m_vt, &VtEmulator::cbOutput, this);

    m_screen = vterm_obtain_screen(m_vt);
    vterm_screen_set_callbacks(m_screen, &s_screenCallbacks, this);
    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_SCROLL);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_reset(m_screen, 1);
    vterm_screen_flush_damage(m_screen);
}

VtEmulator::~VtEmulator()
{
    // Screen is owned by the VTerm; freeing vt frees the screen.
    if (m_vt)
        vterm_free(m_vt);
}

// ---------------------------------------------------------------------------
// Raw input: pass 1 scans the chunk for OSC 133 / sixel DCS boundaries,
// pass 2 replays the chunk to libvterm in runs split at event boundaries so
// that marker placement uses the cursor position at the sequence's receipt.
// ---------------------------------------------------------------------------
void VtEmulator::feed(const char* data, size_t len)
{
    if (!data || len == 0)
        return;

    std::vector<ScanEvent> events;
    bool becameActive = false;

    {
        QMutexLocker lock(&m_mutex);

        // Pass 1: byte-stream scan (state survives across feed() calls).
        for (size_t i = 0; i < len; ++i)
            scanByte(static_cast<unsigned char>(data[i]), i, events);

        // Pass 2: forward everything to libvterm, applying marks at the
        // right moments. libvterm ignores unknown OSC and DCS strings, so
        // forwarding the raw bytes is harmless.
        size_t written = 0;
        for (const ScanEvent& ev : events) {
            const size_t stop = std::min(ev.endOffset, len);
            if (stop > written) {
                vterm_input_write(m_vt, data + written, stop - written);
                vterm_screen_flush_damage(m_screen);
                written = stop;
            }
            if (!m_shellIntegrationActive
                && (ev.type == ScanEvent::PromptStart || ev.type == ScanEvent::CommandStart
                    || ev.type == ScanEvent::CommandDone)) {
                m_shellIntegrationActive = true;
                becameActive = true;
            }
            applyScanEvent(ev);
        }
        if (written < len) {
            vterm_input_write(m_vt, data + written, len - written);
            vterm_screen_flush_damage(m_screen);
        }

        // A line restored from scrollback is positioned after the moverect
        // notification; apply its mark now that the screen settled.
        if (m_pendingPopMarkValid) {
            if (!m_screenMarks.empty())
                m_screenMarks[0] = m_pendingPopMark;
            m_pendingPopMarkValid = false;
        }
    }

    // Emit notifications outside the lock (receivers are queued connections).
    if (becameActive)
        emit shellIntegrationChanged();
    for (const ScanEvent& ev : events) {
        switch (ev.type) {
        case ScanEvent::CommandStart:
            emit commandStarted();
            break;
        case ScanEvent::CommandDone:
            if (ev.exitCode >= 0)
                emit commandFinished(ev.exitCode);
            break;
        case ScanEvent::SixelImage:
            emit sixelImageReady(ev.image);
            break;
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------------------
void VtEmulator::inputBytes(const QByteArray& bytes)
{
    emit outputReady(bytes);
}

void VtEmulator::keyPress(int vtermKey, int mods)
{
    QMutexLocker lock(&m_mutex);
    vterm_keyboard_key(m_vt, VTermKey(vtermKey), VTermModifier(mods));
}

void VtEmulator::unicodePress(uint codepoint, int mods)
{
    QMutexLocker lock(&m_mutex);
    vterm_keyboard_unichar(m_vt, codepoint, VTermModifier(mods));
}

void VtEmulator::mouseMove(int row, int col, int mods)
{
    if (m_mouseMode == 0)
        return;
    QMutexLocker lock(&m_mutex);
    vterm_mouse_move(m_vt, row, col, VTermModifier(mods));
}

void VtEmulator::mouseButton(int button, bool pressed, int mods, int row, int col)
{
    if (m_mouseMode > 0) {
        QMutexLocker lock(&m_mutex);
        if (m_mouseMode >= 1)
            vterm_mouse_move(m_vt, row, col, VTermModifier(mods));
        vterm_mouse_button(m_vt, button, pressed ? 1 : 0, VTermModifier(mods));
    }
}

void VtEmulator::pasteText(const QString& text)
{
    QMutexLocker lock(&m_mutex);
    vterm_keyboard_start_paste(m_vt);
    // Surrogate pairs are re-composed so astral codepoints survive.
    QString remaining = text;
    while (!remaining.isEmpty()) {
        const QChar head = remaining.at(0);
        uint cp = head.unicode();
        if (head.isHighSurrogate() && remaining.size() > 1 && remaining.at(1).isLowSurrogate()) {
            cp = QChar::surrogateToUcs4(head, remaining.at(1));
            remaining.remove(0, 2);
        } else {
            remaining.remove(0, 1);
        }
        vterm_keyboard_unichar(m_vt, cp, VTERM_MOD_NONE);
    }
    vterm_keyboard_end_paste(m_vt);
}

void VtEmulator::resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0 || (cols == m_cols && rows == m_rows))
        return;
    QMutexLocker lock(&m_mutex);
    m_cols = cols;
    m_rows = rows;
    m_screenMarks.resize(size_t(rows), RowMark::None);
    vterm_set_size(m_vt, rows, cols);
    vterm_screen_flush_damage(m_screen);
    if (m_pendingPopMarkValid) {
        if (!m_screenMarks.empty())
            m_screenMarks[0] = m_pendingPopMark;
        m_pendingPopMarkValid = false;
    }
    emit emulatorResized(cols, rows);
    emit damage();
}

VtEmulator::Cell VtEmulator::cellAt(int row, int col) const
{
    QMutexLocker lock(&m_mutex);
    Cell out;
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols)
        return out;
    VTermPos pos { row, col };
    VTermScreenCell cell;
    if (!vterm_screen_get_cell(m_screen, pos, &cell))
        return out;
    QString text;
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
        text += QChar(cell.chars[i]);
    if (text.isEmpty())
        text = QStringLiteral(" ");
    out.text = text;
    out.width = cell.width;
    out.bold = cell.attrs.bold;
    out.underline = cell.attrs.underline;
    out.italic = cell.attrs.italic;
    out.blink = cell.attrs.blink;
    out.reverse = cell.attrs.reverse;
    out.strike = cell.attrs.strike;
    out.fg = resolveColor(cell.fg, true);
    out.bg = resolveColor(cell.bg, false);
    return out;
}

int VtEmulator::scrollbackCount() const
{
    QMutexLocker lock(&m_mutex);
    return int(m_scrollback.size());
}

VtEmulator::Cell VtEmulator::scrollbackCell(int lineIndex, int col) const
{
    QMutexLocker lock(&m_mutex);
    Cell out;
    if (lineIndex < 0 || lineIndex >= int(m_scrollback.size()))
        return out;
    const auto& line = m_scrollback[size_t(lineIndex)];
    if (col < 0 || col >= int(line.size()))
        return out;
    return line[size_t(col)];
}

void VtEmulator::clearScrollback()
{
    QMutexLocker lock(&m_mutex);
    m_scrollback.clear();
    m_scrollbackMarks.clear();
    emit damage();
}

void VtEmulator::setPaletteColor(int index, const QColor& c)
{
    if (index < 0 || index > 15)
        return;
    m_palette[index] = c;
    emit damage();
}

QColor VtEmulator::indexedColor(int index) const
{
    if (index < 0 || index > 257)
        return QColor(0, 0, 0);
    return m_palette[index];
}

QColor VtEmulator::resolveColor(const VTermColor& color, bool isForeground) const
{
    if (VTERM_COLOR_IS_DEFAULT_FG(&color))
        return m_palette[256];
    if (VTERM_COLOR_IS_DEFAULT_BG(&color))
        return m_palette[257];
    if (VTERM_COLOR_IS_INDEXED(&color)) {
        int idx = color.indexed.idx;
        if (idx < 0 || idx > 255)
            idx = 0;
        return m_palette[idx];
    }
    if (VTERM_COLOR_IS_RGB(&color))
        return QColor(color.rgb.red, color.rgb.green, color.rgb.blue);
    return isForeground ? m_palette[256] : m_palette[257];
}

// ---------------------------------------------------------------------------
// find-in-buffer (Task 2-a)
// ---------------------------------------------------------------------------
QString VtEmulator::rowTextUnlocked(int absoluteRow, std::vector<int>* colMap) const
{
    QString text;
    if (absoluteRow < 0)
        return text;
    const int sb = int(m_scrollback.size());
    if (colMap)
        colMap->clear();
    if (absoluteRow < sb) {
        const auto& line = m_scrollback[size_t(absoluteRow)];
        for (int c = 0; c < int(line.size()); ++c) {
            const QString& t = line[size_t(c)].text;
            if (colMap)
                colMap->insert(colMap->end(), size_t(t.size()), c);
            text += t;
        }
    } else {
        const int sr = absoluteRow - sb;
        if (sr >= m_rows)
            return text;
        for (int c = 0; c < m_cols; ++c) {
            VTermPos pos { sr, c };
            VTermScreenCell cell;
            if (!vterm_screen_get_cell(m_screen, pos, &cell))
                break;
            QString t;
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
                t += QChar(cell.chars[i]);
            if (t.isEmpty())
                t = QStringLiteral(" ");
            if (colMap)
                colMap->insert(colMap->end(), size_t(t.size()), c);
            text += t;
        }
    }
    return text;
}

QString VtEmulator::rowText(int absoluteRow) const
{
    QMutexLocker lock(&m_mutex);
    return rowTextUnlocked(absoluteRow, nullptr);
}

QVector<QPair<int, int>> VtEmulator::find(const QString& needle, bool caseSensitive) const
{
    QVector<QPair<int, int>> matches;
    if (needle.isEmpty())
        return matches;

    QMutexLocker lock(&m_mutex);
    const int total = int(m_scrollback.size()) + m_rows;
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    std::vector<int> colMap;
    for (int r = 0; r < total; ++r) {
        const QString text = rowTextUnlocked(r, &colMap);
        int pos = int(text.indexOf(needle, 0, cs));
        while (pos >= 0) {
            const int col = size_t(pos) < colMap.size() ? colMap[size_t(pos)] : pos;
            matches.append(QPair<int, int>(r, col));
            pos = int(text.indexOf(needle, pos + qMax(1, needle.size()), cs));
        }
    }
    return matches;
}

// ---------------------------------------------------------------------------
// OSC 133 shell integration (Task 2-a)
// ---------------------------------------------------------------------------
VtEmulator::RowMark VtEmulator::rowMark(int absoluteRow) const
{
    QMutexLocker lock(&m_mutex);
    const int sb = int(m_scrollback.size());
    if (absoluteRow < 0)
        return RowMark::None;
    if (absoluteRow < sb) {
        if (size_t(absoluteRow) >= m_scrollbackMarks.size())
            return RowMark::None;
        return m_scrollbackMarks[size_t(absoluteRow)];
    }
    const int sr = absoluteRow - sb;
    if (sr >= m_rows || sr < 0 || size_t(sr) >= m_screenMarks.size())
        return RowMark::None;
    return m_screenMarks[size_t(sr)];
}

int VtEmulator::previousPromptRow(int currentViewportTop) const
{
    QMutexLocker lock(&m_mutex);
    const int sb = int(m_scrollback.size());
    const int total = sb + m_rows;
    for (int r = qMin(currentViewportTop, total) - 1; r >= 0; --r) {
        const RowMark m = r < sb ? (size_t(r) < m_scrollbackMarks.size()
                                        ? m_scrollbackMarks[size_t(r)] : RowMark::None)
                                 : (size_t(r - sb) < m_screenMarks.size()
                                        ? m_screenMarks[size_t(r - sb)] : RowMark::None);
        if (m == RowMark::PromptStart)
            return r;
    }
    return -1;
}

int VtEmulator::nextPromptRow(int currentViewportTop) const
{
    QMutexLocker lock(&m_mutex);
    const int sb = int(m_scrollback.size());
    const int total = sb + m_rows;
    for (int r = qMax(0, currentViewportTop) + 1; r < total; ++r) {
        const RowMark m = r < sb ? (size_t(r) < m_scrollbackMarks.size()
                                        ? m_scrollbackMarks[size_t(r)] : RowMark::None)
                                 : (size_t(r - sb) < m_screenMarks.size()
                                        ? m_screenMarks[size_t(r - sb)] : RowMark::None);
        if (m == RowMark::PromptStart)
            return r;
    }
    return -1;
}

bool VtEmulator::shellIntegrationActive() const
{
    QMutexLocker lock(&m_mutex);
    return m_shellIntegrationActive;
}

void VtEmulator::setScreenMark(int row, RowMark mark)
{
    if (row < 0 || row >= m_rows || row >= int(m_screenMarks.size()))
        return;
    m_screenMarks[size_t(row)] = mark;
}

void VtEmulator::moveScreenMarks(const VTermRect& dest, const VTermRect& src)
{
    if (m_screenMarks.size() != size_t(m_rows))
        return;
    std::vector<RowMark> next(m_screenMarks.size(), RowMark::None);
    for (int r = 0; r < m_rows; ++r) {
        const bool inDest = r >= dest.start_row && r < dest.end_row;
        const bool inSrc = r >= src.start_row && r < src.end_row;
        if (!inDest && !inSrc)
            next[size_t(r)] = m_screenMarks[size_t(r)];
    }
    const int count = std::min(dest.end_row - dest.start_row, src.end_row - src.start_row);
    for (int i = 0; i < count; ++i) {
        const int dr = dest.start_row + i;
        const int sr = src.start_row + i;
        if (dr >= 0 && dr < m_rows && sr >= 0 && sr < m_rows)
            next[size_t(dr)] = m_screenMarks[size_t(sr)];
    }
    m_screenMarks = std::move(next);
}

// ---------------------------------------------------------------------------
// Byte-stream scanner (Task 2-a): extracts OSC 133 payloads and sixel DCS
// bodies from the raw stream. The state machine persists across feed() calls
// so sequences split over chunk boundaries are handled.
// ---------------------------------------------------------------------------
void VtEmulator::scanByte(unsigned char b, size_t offset, std::vector<ScanEvent>& events)
{
    switch (m_scanState) {
    case ScanState::Ground:
        if (b == 0x1b)
            m_scanState = ScanState::Esc;
        break;

    case ScanState::Esc:
        switch (b) {
        case ']': // OSC
            m_scanState = ScanState::Osc;
            m_oscBuffer.clear();
            m_captureOverflow = false;
            break;
        case 'P': // DCS
            m_scanState = ScanState::DcsIntro;
            m_dcsIntro.clear();
            break;
        default: // single-char escape / CSI / charset: irrelevant to us
            m_scanState = ScanState::Ground;
            break;
        }
        break;

    case ScanState::Osc:
        if (b == 0x07) { // BEL terminator
            completeOsc(offset + 1, events);
            m_scanState = ScanState::Ground;
        } else if (b == 0x1b) {
            m_scanState = ScanState::OscEsc;
        } else if (b == 0x18 || b == 0x1a) { // CAN/SUB abort
            m_oscBuffer.clear();
            m_scanState = ScanState::Ground;
        } else if (m_oscBuffer.size() < qsizetype(kMaxOscBytes)) {
            m_oscBuffer.append(char(b));
        } else {
            m_captureOverflow = true; // keep consuming, drop content
        }
        break;

    case ScanState::OscEsc:
        if (b == '\\') { // ST
            completeOsc(offset + 1, events);
            m_scanState = ScanState::Ground;
        } else {
            // ESC terminated the string; the current byte starts a new
            // escape sequence.
            completeOsc(offset, events);
            m_scanState = ScanState::Esc;
            scanByte(b, offset, events);
        }
        break;

    case ScanState::DcsIntro:
        if (b == 'q') { // sixel introducer final byte
            m_sixelTransparent = SixelDecoder::introRequestsTransparency(m_dcsIntro);
            m_sixelBuffer.clear();
            m_captureOverflow = false;
            m_scanState = ScanState::DcsSixel;
        } else if (b >= 0x40 && b <= 0x7e) {
            // Other DCS sequences are not ours; their bytes flow through.
            m_scanState = ScanState::Ground;
        } else if (b == 0x1b) {
            m_scanState = ScanState::Esc;
        } else if (b == 0x18 || b == 0x1a) {
            m_scanState = ScanState::Ground;
        } else if (m_dcsIntro.size() < 64) {
            m_dcsIntro.append(char(b));
        } else {
            m_scanState = ScanState::Ground; // not a sane sixel introducer
        }
        break;

    case ScanState::DcsSixel:
        if (b == 0x1b) {
            m_scanState = ScanState::DcsEsc;
        } else if (b == 0x07) { // BEL as terminator (tolerated)
            finishSixel(offset + 1, events);
            m_scanState = ScanState::Ground;
        } else if (b == 0x18 || b == 0x1a) { // CAN/SUB abort
            m_sixelBuffer.clear();
            m_captureOverflow = false;
            m_scanState = ScanState::Ground;
        } else if (m_sixelBuffer.size() < qsizetype(kMaxSixelBytes)) {
            m_sixelBuffer.append(char(b));
        } else {
            m_captureOverflow = true; // keep consuming, drop content
        }
        break;

    case ScanState::DcsEsc:
        if (b == '\\') { // ST
            finishSixel(offset + 1, events);
            m_scanState = ScanState::Ground;
        } else {
            // Defensive: ESC inside the payload terminates it.
            finishSixel(offset, events);
            m_scanState = ScanState::Esc;
            scanByte(b, offset, events);
        }
        break;
    }
}

void VtEmulator::completeOsc(size_t endOffset, std::vector<ScanEvent>& events)
{
    if (!m_captureOverflow && m_oscBuffer.size() >= 5
        && m_oscBuffer.startsWith("133;")) {
        const char cmd = m_oscBuffer.at(4);
        switch (cmd) {
        case 'A':
            events.push_back({ ScanEvent::PromptStart, -1, QImage(), endOffset });
            break;
        case 'B':
            events.push_back({ ScanEvent::OutputStart, -1, QImage(), endOffset });
            break;
        case 'C':
            events.push_back({ ScanEvent::CommandStart, -1, QImage(), endOffset });
            break;
        case 'D': {
            int exitCode = -1;
            if (m_oscBuffer.size() > 6 && m_oscBuffer.at(5) == ';') {
                QByteArray tail = m_oscBuffer.mid(6);
                const int semi = tail.indexOf(';');
                if (semi >= 0)
                    tail.truncate(semi);
                bool ok = false;
                exitCode = QString::fromLatin1(tail).trimmed().toInt(&ok);
                if (!ok)
                    exitCode = -1;
            }
            events.push_back({ ScanEvent::CommandDone, exitCode, QImage(), endOffset });
            break;
        }
        default: // 'P' (prompt line props), 'E', ... ignored for now
            break;
        }
    }
    m_oscBuffer.clear();
    m_captureOverflow = false;
}

void VtEmulator::finishSixel(size_t endOffset, std::vector<ScanEvent>& events)
{
    if (!m_captureOverflow && !m_sixelBuffer.isEmpty()) {
        const QImage img = SixelDecoder::decode(m_sixelBuffer, m_sixelTransparent);
        if (!img.isNull())
            events.push_back({ ScanEvent::SixelImage, -1, img, endOffset });
    }
    m_sixelBuffer.clear();
    m_captureOverflow = false;
}

void VtEmulator::applyScanEvent(const ScanEvent& ev)
{
    RowMark mark = RowMark::None;
    switch (ev.type) {
    case ScanEvent::PromptStart:
        mark = RowMark::PromptStart;
        break;
    case ScanEvent::OutputStart:
        mark = RowMark::OutputStart;
        break;
    case ScanEvent::CommandStart:
        mark = RowMark::CommandStart;
        break;
    case ScanEvent::CommandDone:
        mark = RowMark::CommandDone;
        break;
    case ScanEvent::SixelImage:
        return; // no row mark; the image is delivered via sixelImageReady()
    }
    setScreenMark(m_cursorRow, mark);
}

// ---------------------------------------------------------------------------
// libvterm callbacks
// ---------------------------------------------------------------------------
int VtEmulator::cbDamage(VTermRect, void* user)
{
    auto* self = static_cast<VtEmulator*>(user);
    emit self->damage();
    return 1;
}

int VtEmulator::cbMoveRect(VTermRect dest, VTermRect src, void* user)
{
    auto* self = static_cast<VtEmulator*>(user);
    // Keep shell-integration markers aligned with the moved content.
    self->moveScreenMarks(dest, src);
    emit self->damage();
    return 1;
}

int VtEmulator::cbMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user)
{
    auto* self = static_cast<VtEmulator*>(user);
    self->m_cursorRow = pos.row;
    self->m_cursorCol = pos.col;
    self->m_cursorVisible = visible != 0;
    emit self->damage();
    return 1;
}

int VtEmulator::cbSetTermProp(VTermProp prop, VTermValue* val, void* user)
{
    auto* self = static_cast<VtEmulator*>(user);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        self->m_cursorVisible = val->boolean != 0;
        break;
    case VTERM_PROP_MOUSE:
        self->m_mouseMode = val->number;
        break;
    case VTERM_PROP_TITLE: {
        const VTermStringFragment& frag = val->string;
        if (frag.initial)
            self->m_titleBuffer.clear();
        if (frag.str && frag.len)
            self->m_titleBuffer.append(frag.str, int(frag.len));
        if (frag.final) {
            self->m_title = QString::fromUtf8(self->m_titleBuffer);
            emit self->titleChanged(self->m_title);
        }
        break;
    }
    default:
        break;
    }
    emit self->damage();
    return 1;
}

int VtEmulator::cbBell(void* user)
{
    auto* self = static_cast<VtEmulator*>(user);
    emit self->bell();
    return 1;
}

int VtEmulator::cbResize(int rows, int cols, void* user)
{
    Q_UNUSED(rows);
    Q_UNUSED(cols);
    auto* self = static_cast<VtEmulator*>(user);
    emit self->damage();
    return 1;
}

int VtEmulator::cbPushLine(int cols, const VTermScreenCell* cells, void* user)
{
    auto* self = static_cast<VtEmulator*>(user);
    self->pushScrollbackLine(cols, cells);
    return 1;
}

int VtEmulator::cbPopLine(int cols, VTermScreenCell* cells, void* user)
{
    auto* self = static_cast<VtEmulator*>(user);
    return self->popScrollbackLine(cols, cells) ? 1 : 0;
}

int VtEmulator::cbClear(void* user)
{
    auto* self = static_cast<VtEmulator*>(user);
    self->m_scrollback.clear();
    self->m_scrollbackMarks.clear();
    std::fill(self->m_screenMarks.begin(), self->m_screenMarks.end(), RowMark::None);
    emit self->damage();
    return 1;
}

void VtEmulator::cbOutput(const char* bytes, size_t len, void* user)
{
    auto* self = static_cast<VtEmulator*>(user);
    emit self->outputReady(QByteArray(bytes, int(len)));
}

void VtEmulator::pushScrollbackLine(int cols, const VTermScreenCell* cells)
{
    std::vector<Cell> line;
    line.reserve(size_t(cols));
    for (int c = 0; c < cols; ++c) {
        Cell cell;
        QString text;
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cells[c].chars[i]; ++i)
            text += QChar(cells[c].chars[i]);
        cell.text = text.isEmpty() ? QStringLiteral(" ") : text;
        cell.width = cells[c].width;
        cell.bold = cells[c].attrs.bold;
        cell.underline = cells[c].attrs.underline;
        cell.italic = cells[c].attrs.italic;
        cell.reverse = cells[c].attrs.reverse;
        cell.strike = cells[c].attrs.strike;
        cell.fg = resolveColor(cells[c].fg, true);
        cell.bg = resolveColor(cells[c].bg, false);
        line.push_back(std::move(cell));
    }
    m_scrollback.push_back(std::move(line));
    // The pushed top-row mark travels with the content into scrollback; the
    // remaining screen rows are re-aligned by the moverect notification.
    m_scrollbackMarks.push_back(m_screenMarks.empty() ? RowMark::None
                                                      : m_screenMarks.front());
    while (int(m_scrollback.size()) > m_scrollbackLimit) {
        m_scrollback.pop_front();
        if (!m_scrollbackMarks.empty())
            m_scrollbackMarks.pop_front();
    }
}

bool VtEmulator::popScrollbackLine(int cols, VTermScreenCell* cells)
{
    if (m_scrollback.empty())
        return false;
    const auto& line = m_scrollback.front();
    for (int c = 0; c < cols && c < int(line.size()); ++c) {
        VTermScreenCell& cell = cells[c];
        memset(&cell, 0, sizeof(cell));
        const Cell& src = line[size_t(c)];
        cell.chars[0] = src.text.isEmpty() ? uint32_t(' ') : src.text.at(0).unicode();
        cell.width = src.width;
        cell.attrs.bold = src.bold;
        cell.attrs.underline = src.underline;
        cell.attrs.italic = src.italic;
        cell.attrs.reverse = src.reverse;
        cell.attrs.strike = src.strike;
    }
    for (int c = int(line.size()); c < cols; ++c) {
        memset(&cells[c], 0, sizeof(cells[c]));
        cells[c].chars[0] = ' ';
    }
    // The restored line takes its mark back (applied after the pending move).
    m_pendingPopMark = m_scrollbackMarks.empty() ? RowMark::None : m_scrollbackMarks.front();
    m_pendingPopMarkValid = true;
    m_scrollback.pop_front();
    if (!m_scrollbackMarks.empty())
        m_scrollbackMarks.pop_front();
    return true;
}

} // namespace eclipse
