#include "VtEmulator.h"

#include <QMutexLocker>


#include <vterm.h>
#include <vterm_keycodes.h>

namespace eclipse {

const VTermScreenCallbacks VtEmulator::s_screenCallbacks = {
    cbDamage, cbMoveRect, cbMoveCursor, cbSetTermProp, cbBell, cbResize, cbPushLine, cbPopLine, cbClear,
};

namespace {

constexpr int kScrollDamageEveryN = 1;

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

void VtEmulator::feed(const char* data, size_t len)
{
    if (!data || len == 0)
        return;
    QMutexLocker lock(&m_mutex);
    vterm_input_write(m_vt, data, len);
    vterm_screen_flush_damage(m_screen);
}

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
    vterm_set_size(m_vt, rows, cols);
    vterm_screen_flush_damage(m_screen);
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
// libvterm callbacks
// ---------------------------------------------------------------------------
int VtEmulator::cbDamage(VTermRect, void* user)
{
    auto* self = static_cast<VtEmulator*>(user);
    emit self->damage();
    return 1;
}

int VtEmulator::cbMoveRect(VTermRect, VTermRect, void* user)
{
    auto* self = static_cast<VtEmulator*>(user);
    emit self->damage();
    return 1;
}

int VtEmulator::cbMoveCursor(VTermPos pos, VTermPos, int visible, void* user)
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
    while (int(m_scrollback.size()) > m_scrollbackLimit)
        m_scrollback.pop_front();
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
    m_scrollback.pop_front();
    return true;
}

} // namespace eclipse
