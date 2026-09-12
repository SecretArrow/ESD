#include "TermItem.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPaintEvent>

#include "../common/Utils.h"
#include "../core/logging/Logger.h"
#include "../core/settings/Settings.h"
#include "vterm_keycodes.h"

namespace eclipse {

namespace {

// Qt key -> VTERM_KEY
int mapSpecialKey(int key, bool& handled)
{
    handled = true;
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter: return VTERM_KEY_ENTER;
    case Qt::Key_Tab: return VTERM_KEY_TAB;
    case Qt::Key_Backspace: return VTERM_KEY_BACKSPACE;
    case Qt::Key_Escape: return VTERM_KEY_ESCAPE;
    case Qt::Key_Up: return VTERM_KEY_UP;
    case Qt::Key_Down: return VTERM_KEY_DOWN;
    case Qt::Key_Left: return VTERM_KEY_LEFT;
    case Qt::Key_Right: return VTERM_KEY_RIGHT;
    case Qt::Key_Insert: return VTERM_KEY_INS;
    case Qt::Key_Delete: return VTERM_KEY_DEL;
    case Qt::Key_Home: return VTERM_KEY_HOME;
    case Qt::Key_End: return VTERM_KEY_END;
    case Qt::Key_PageUp: return VTERM_KEY_PAGEUP;
    case Qt::Key_PageDown: return VTERM_KEY_PAGEDOWN;
    case Qt::Key_F1: return VTERM_KEY_FUNCTION(1);
    case Qt::Key_F2: return VTERM_KEY_FUNCTION(2);
    case Qt::Key_F3: return VTERM_KEY_FUNCTION(3);
    case Qt::Key_F4: return VTERM_KEY_FUNCTION(4);
    case Qt::Key_F5: return VTERM_KEY_FUNCTION(5);
    case Qt::Key_F6: return VTERM_KEY_FUNCTION(6);
    case Qt::Key_F7: return VTERM_KEY_FUNCTION(7);
    case Qt::Key_F8: return VTERM_KEY_FUNCTION(8);
    case Qt::Key_F9: return VTERM_KEY_FUNCTION(9);
    case Qt::Key_F10: return VTERM_KEY_FUNCTION(10);
    case Qt::Key_F11: return VTERM_KEY_FUNCTION(11);
    case Qt::Key_F12: return VTERM_KEY_FUNCTION(12);
    default:
        handled = false;
        return -1;
    }
}

int qtModsToVterm(int qtMods)
{
    int m = VTERM_MOD_NONE;
    if (qtMods & Qt::ShiftModifier)
        m |= VTERM_MOD_SHIFT;
    if (qtMods & Qt::AltModifier)
        m |= VTERM_MOD_ALT;
    if (qtMods & Qt::ControlModifier)
        m |= VTERM_MOD_CTRL;
    return m;
}

} // namespace

TermItem::TermItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setFlag(ItemAcceptsInputMethod, true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setRenderTarget(QQuickPaintedItem::Image);
    setAntialiasing(false);

    m_fontFamily = Settings::instance().terminalFontFamily();
    m_fontSize = Settings::instance().terminalFontSize();

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(530);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        if (Settings::instance().terminalCursorBlink()) {
            m_cursorBlinkOn = !m_cursorBlinkOn;
            update();
        }
    });
    m_blinkTimer->start();
}

SshSession* TermItem::session() const
{
    return m_session;
}

bool TermItem::recording() const
{
    return m_recording;
}

TermItem::~TermItem()
{
    if (m_session && m_cid >= 0) {
        const int cid = m_cid;
        QMetaObject::invokeMethod(m_session->worker(), [session = m_session->worker(), cid]() {
            session->channelClose(cid);
        }, Qt::QueuedConnection);
    }
}

void TermItem::setSession(SshSession* s)
{
    if (m_session == s)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = s;
    emit sessionChanged();

    if (!m_vt)
        m_vt = std::make_shared<VtEmulator>(80, 24);

    connect(m_vt.get(), &VtEmulator::damage, this, [this]() { update(); }, Qt::QueuedConnection);
    connect(m_vt.get(), &VtEmulator::outputReady, this, &TermItem::writeOut, Qt::QueuedConnection);
    connect(m_vt.get(), &VtEmulator::titleChanged, this, &TermItem::titleChanged, Qt::QueuedConnection);

    if (m_session) {
        connect(m_session, &SshSession::terminalOpened, this, &TermItem::attachChannel, Qt::QueuedConnection);
        connect(m_session, &SshSession::terminalData, this,
                [this](int cid, const QByteArray& data, bool isStderr) {
                    Q_UNUSED(isStderr);
                    if (cid != m_cid || !m_vt || data.isEmpty())
                        return;
                    m_vt->feed(data.constData(), size_t(data.size()));
                    if (m_recording && m_recordingFile.isOpen()) {
                        m_recordingFile.write(data);
                        m_recordingFile.flush();
                    }
                },
                Qt::QueuedConnection);
        connect(m_session, &SshSession::terminalClosed, this, [this](int cid) {
            if (cid == m_cid)
                m_cid = -1;
        }, Qt::QueuedConnection);
        connect(m_session, &SshSession::disconnected, this, [this]() {
            m_cid = -1;
            update();
        }, Qt::QueuedConnection);
        // Request a terminal on the active session (worker must be connected)
        if (m_session->isConnected())
            m_session->openTerminal(80, 24);
    }
    update();
}

QString TermItem::title() const
{
    return m_vt ? m_vt->terminalTitle() : QString();
}

void TermItem::attachChannel(int cid)
{
    m_cid = cid;
    emit channelAttached(cid);
    // pty size matches widget
    if (m_vt)
        m_vt->resize(effectiveCols(), effectiveRows());
    QMetaObject::invokeMethod(m_session->worker(),
                              [w = m_session->worker(), cid, cols = effectiveCols(), rows = effectiveRows()]() {
                                  w->channelResize(cid, cols, rows);
                              },
                              Qt::QueuedConnection);
    setFocus(true);
    update();
}

void TermItem::setFontFamily(const QString& f)
{
    if (m_fontFamily == f)
        return;
    m_fontFamily = f;
    updateCellMetrics();
    emit fontChanged();
}

void TermItem::setFontSize(int s)
{
    if (m_fontSize == s || s <= 0)
        return;
    m_fontSize = s;
    updateCellMetrics();
    emit fontChanged();
}

void TermItem::setRecording(bool on)
{
    if (m_recording == on)
        return;
    if (on) {
        const QString dir = utils::dataDirectory() + QStringLiteral("/recordings");
        QDir().mkpath(dir);
        m_recordingFile.setFileName(dir + QStringLiteral("/session-%1.log")
                                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"))));
        m_recording = m_recordingFile.open(QIODevice::WriteOnly | QIODevice::Text);
        m_recordingStart = QDateTime::currentDateTime();
    } else {
        m_recordingFile.close();
        m_recording = false;
    }
    emit recordingChanged();
}

void TermItem::updateCellMetrics()
{
    m_font = QFont(m_fontFamily, m_fontSize);
    m_font.setStyleHint(QFont::TypeWriter);
    m_font.setFixedPitch(true);
    QFontMetricsF fm(m_font);
    m_cellWidth = fm.horizontalAdvance(QLatin1Char('W'));
    if (m_cellWidth <= 0)
        m_cellWidth = 9;
    m_cellHeight = fm.height() * 1.15;
    if (m_cellHeight <= 0)
        m_cellHeight = 18;
    if (m_vt && width() > 0 && height() > 0) {
        const int cols = effectiveCols();
        const int rows = effectiveRows();
        m_vt->resize(cols, rows);
        if (m_session && m_cid >= 0) {
            QMetaObject::invokeMethod(m_session->worker(),
                                      [w = m_session->worker(), cid = m_cid, cols, rows]() {
                                          w->channelResize(cid, cols, rows);
                                      },
                                      Qt::QueuedConnection);
        }
    }
    update();
}

int TermItem::effectiveCols() const
{
    return qMax(20, int((width() - 8) / m_cellWidth));
}

int TermItem::effectiveRows() const
{
    return qMax(4, int((height() - 8) / m_cellHeight));
}

void TermItem::writeOut(const QByteArray& bytes)
{
    if (m_session && m_cid >= 0) {
        QMetaObject::invokeMethod(m_session->worker(),
                                  [w = m_session->worker(), cid = m_cid, bytes]() {
                                      w->channelWrite(cid, bytes);
                                  },
                                  Qt::QueuedConnection);
    }
}

void TermItem::sendText(const QString& text)
{
    if (m_vt) {
        const QByteArray utf8 = text.toUtf8();
        m_vt->inputBytes(utf8);
    }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void TermItem::paint(QPainter* painter)
{
    painter->fillRect(boundingRect(), QColor(0x14, 0x17, 0x1e));
    if (!m_vt)
        return;
    painter->setFont(m_font);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const int scrollback = m_vt->scrollbackCount();
    const int viewOffset = qBound(0, m_scrollOffset, scrollback);
    const int rows = effectiveRows();
    const int cols = effectiveCols();

    QFontMetricsF fm(m_font);
    const qreal pad = 4;

    for (int r = 0; r < rows; ++r) {
        const int lineIndex = r - viewOffset; // negative = scrollback region
        for (int c = 0; c < cols; ++c) {
            VtEmulator::Cell cell;
            bool inScrollback = false;
            if (lineIndex < 0) {
                const int sbIndex = scrollback - viewOffset + r;
                if (sbIndex < 0)
                    continue;
                cell = m_vt->scrollbackCell(sbIndex, c);
                inScrollback = true;
            } else {
                cell = m_vt->cellAt(lineIndex, c);
            }

            QColor fg = cell.fg;
            QColor bg = cell.bg;
            if (cell.reverse)
                std::swap(fg, bg);

            const QPointF origin(pad + c * m_cellWidth, pad + r * m_cellHeight);
            const QRectF cellRect(origin, QSizeF(m_cellWidth, m_cellHeight));

            // selection highlight
            bool selected = false;
            if (m_hasSelection && !inScrollback) {
                const int row = lineIndex;
                const bool afterStart = row > m_selStartRow || (row == m_selStartRow && c >= m_selStartCol);
                const bool beforeEnd = row < m_selEndRow || (row == m_selEndRow && c <= m_selEndCol);
                const bool swapped = qMakePair(m_selStartRow, m_selStartCol)
                                     > qMakePair(m_selEndRow, m_selEndCol);
                selected = swapped ? (afterStart && beforeEnd)
                                   : (qMakePair(row, c) >= qMakePair(m_selStartRow, m_selStartCol)
                                      && qMakePair(row, c) <= qMakePair(m_selEndRow, m_selEndCol));
            }
            if (selected)
                bg = QColor(0x3b, 0x52, 0x7a);

            if (bg != QColor(0x14, 0x17, 0x1e))
                painter->fillRect(cellRect, bg);

            if (cell.text.isEmpty() || cell.text == QStringLiteral(" ")) {
                continue;
            }
            painter->setPen(fg);
            QString t = cell.text;
            painter->drawText(cellRect, Qt::AlignVCenter | Qt::AlignLeft, t);
            if (cell.underline) {
                painter->fillRect(QRectF(origin.x(), origin.y() + m_cellHeight - 2,
                                         m_cellWidth, 1),
                                  fg);
            }
            if (cell.strike) {
                painter->fillRect(QRectF(origin.x(), origin.y() + m_cellHeight / 2,
                                         m_cellWidth, 1),
                                  fg);
            }
        }
    }

    // Cursor
    if (m_cid >= 0 && m_cursorBlinkOn && m_scrollOffset == 0) {
        const int cr = m_vt->cursorRow();
        const int cc = m_vt->cursorCol();
        if (cr >= 0 && cr < rows && cc >= 0 && cc < cols) {
            const QRectF cursorRect(pad + cc * m_cellWidth, pad + cr * m_cellHeight,
                                    m_cellWidth, m_cellHeight);
            painter->fillRect(cursorRect, QColor(0xe6, 0xe8, 0xee, 200));
        }
    }
}

QString TermItem::selectedText() const
{
    if (!m_hasSelection)
        return {};
    int r1 = m_selStartRow, c1 = m_selStartCol;
    int r2 = m_selEndRow, c2 = m_selEndCol;
    if (qMakePair(r1, c1) > qMakePair(r2, c2)) {
        std::swap(r1, r2);
        std::swap(c1, c2);
    }
    QString out;
    for (int r = r1; r <= r2; ++r) {
        for (int c = 0; c < effectiveCols(); ++c) {
            const bool inSel = (r > r1 || (r == r1 && c >= c1))
                               && (r < r2 || (r == r2 && c <= c2));
            if (!inSel)
                continue;
            out += m_vt ? m_vt->cellAt(r, c).text : QString();
        }
        while (out.endsWith(QLatin1Char(' ')))
            out.chop(1);
        out += QLatin1Char('\n');
    }
    return out;
}

void TermItem::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void TermItem::pasteClipboard()
{
    if (!m_vt)
        return;
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty())
        return;
    m_vt->pasteText(text);
}

void TermItem::selectAll()
{
    if (!m_vt)
        return;
    m_selStartRow = 0;
    m_selStartCol = 0;
    m_selEndRow = effectiveRows() - 1;
    m_selEndCol = effectiveCols() - 1;
    m_hasSelection = true;
    update();
}

void TermItem::clearScrollback()
{
    if (m_vt)
        m_vt->clearScrollback();
}

void TermItem::scrollToBottom()
{
    m_scrollOffset = 0;
    update();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void TermItem::keyPressEvent(QKeyEvent* event)
{
    if (!m_vt) {
        QQuickPaintedItem::keyPressEvent(event);
        return;
    }
    const int key = event->key();
    const int mods = event->modifiers();

    // copy/paste
    if (mods & Qt::ControlModifier && key == Qt::Key_C && mods & Qt::ShiftModifier) {
        copySelection();
        event->accept();
        return;
    }
    if (mods & Qt::ControlModifier && key == Qt::Key_V && mods & Qt::ShiftModifier) {
        pasteClipboard();
        event->accept();
        return;
    }

    bool handled = false;
    const int vk = mapSpecialKey(key, handled);
    if (handled) {
        // Shift+PageUp/Down scrolls the buffer
        if ((key == Qt::Key_PageUp) && (mods & Qt::ShiftModifier)) {
            m_scrollOffset += effectiveRows() / 2;
            update();
            event->accept();
            return;
        }
        if ((key == Qt::Key_PageDown) && (mods & Qt::ShiftModifier)) {
            m_scrollOffset = qMax(0, m_scrollOffset - effectiveRows() / 2);
            update();
            event->accept();
            return;
        }
        m_vt->keyPress(vk, qtModsToVterm(mods));
        m_scrollOffset = 0;
        event->accept();
        update();
        return;
    }

    const QString text = event->text();
    if (!text.isEmpty()) {
        // Ctrl+letter -> control code
        if (mods & Qt::ControlModifier) {
            const char16_t up = text.at(0).toUpper().unicode();
            if (up >= 'A' && up <= 'Z') {
                m_vt->unicodePress(uint(up - 'A' + 1), VTERM_MOD_CTRL);
                event->accept();
                update();
                return;
            }
            if (up == ' ') { // Ctrl+Space = NUL
                m_vt->unicodePress(0, VTERM_MOD_CTRL);
                event->accept();
                update();
                return;
            }
        }
        for (const QChar& ch : text) {
            if (ch.isHighSurrogate() || ch.isLowSurrogate())
                continue;
            m_vt->unicodePress(ch.unicode(), qtModsToVterm(mods & ~Qt::ControlModifier));
        }
        m_scrollOffset = 0;
        event->accept();
        update();
        return;
    }

    QQuickPaintedItem::keyPressEvent(event);
}

void TermItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        pasteClipboard();
        event->accept();
        return;
    }
    if (m_vt && m_vt->mouseReportingEnabled() && m_scrollOffset == 0) {
        int row, col;
        pixelToCell(event->position(), &row, &col);
        m_vt->mouseButton(int(event->button()) - 1, true, 0, row, col);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        int row, col;
        pixelToCell(event->position(), &row, &col);
        m_selStartRow = m_selEndRow = row;
        m_selStartCol = m_selEndCol = col;
        m_hasSelection = true;
        m_selecting = true;
        forceActiveFocus();
        event->accept();
        update();
        return;
    }
    QQuickPaintedItem::mousePressEvent(event);
}

void TermItem::mouseMoveEvent(QMouseEvent* event)
{
    int row, col;
    pixelToCell(event->position(), &row, &col);
    if (m_vt && m_vt->mouseReportingEnabled() && m_scrollOffset == 0 && !(event->buttons() & Qt::LeftButton)) {
        m_vt->mouseMove(row, col, 0);
        event->accept();
        return;
    }
    if (m_selecting) {
        m_selEndRow = row;
        m_selEndCol = col;
        event->accept();
        update();
        return;
    }
    QQuickPaintedItem::mouseMoveEvent(event);
}

void TermItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_vt && m_vt->mouseReportingEnabled() && m_scrollOffset == 0) {
        int row, col;
        pixelToCell(event->position(), &row, &col);
        m_vt->mouseButton(int(event->button()) - 1, false, 0, row, col);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        // copy-on-select option
        event->accept();
        update();
        return;
    }
    QQuickPaintedItem::mouseReleaseEvent(event);
}

void TermItem::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y() > 0 ? 3 : -3;
    if (m_vt && m_vt->mouseReportingEnabled() && m_scrollOffset == 0) {
        // pass through as arrow keys for TUI apps
        const int key = delta > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN;
        for (int i = 0; i < qAbs(delta); ++i)
            m_vt->keyPress(key, 0);
        event->accept();
        return;
    }
    m_scrollOffset = qBound(0, m_scrollOffset + delta, m_vt ? m_vt->scrollbackCount() : 0);
    event->accept();
    update();
}

void TermItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        updateCellMetrics();
        if (m_vt && m_session && m_cid >= 0) {
            const int cols = effectiveCols();
            const int rows = effectiveRows();
            m_vt->resize(cols, rows);
            QMetaObject::invokeMethod(m_session->worker(),
                                      [w = m_session->worker(), cid = m_cid, cols, rows]() {
                                          w->channelResize(cid, cols, rows);
                                      },
                                      Qt::QueuedConnection);
        }
    }
}

void TermItem::pixelToCell(const QPointF& p, int* row, int* col) const
{
    const int scrollback = m_vt ? m_vt->scrollbackCount() : 0;
    const int viewOffset = qBound(0, m_scrollOffset, scrollback);
    *row = qBound(0, int((p.y() - 4) / m_cellHeight), effectiveRows() - 1) - viewOffset;
    *col = qBound(0, int((p.x() - 4) / m_cellWidth), effectiveCols() - 1);
    // row is in "live screen" coordinates when at bottom; when scrolled it can
    // point into scrollback; for selection we keep live coordinates only.
    *row = qBound(0, *row, effectiveRows() - 1);
}

QPointF TermItem::cellToPixel(int row, int col, int) const
{
    return QPointF(4 + col * m_cellWidth, 4 + row * m_cellHeight);
}

void TermItem::appendRecording(const QString&)
{
    // Recording writes happen inline in the terminalData handler.
}

} // namespace eclipse
