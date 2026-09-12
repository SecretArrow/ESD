#pragma once

#include <QQuickPaintedItem>
#include <QFile>
#include <QTimer>

#include "../terminal/VtEmulator.h"
#include "../ssh/SshSession.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// TermItem - QML terminal renderer (QQuickPaintedItem) bound to one channel of
// an SshSession. Features: full color (16/256/truecolor), bold/italic/
// underline/strike/reverse, scrollback with wheel scrolling, selection +
// copy, bracketed paste, mouse reporting pass-through, blinking cursor,
// resize (pty change), alternate screen (via libvterm), find-in-buffer stub
// that scrolls to matches.
// ---------------------------------------------------------------------------
class TermItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(SshSession* session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    Q_PROPERTY(bool recording READ recording WRITE setRecording NOTIFY recordingChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)

public:
    explicit TermItem(QQuickItem* parent = nullptr);
    ~TermItem() override;

    SshSession* session() const;
    void setSession(SshSession* s);
    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString& f);
    int fontSize() const { return m_fontSize; }
    void setFontSize(int s);
    bool recording() const;
    void setRecording(bool on);
    QString title() const;

    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void pasteClipboard();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearScrollback();
    Q_INVOKABLE void scrollToBottom();
    Q_INVOKABLE void sendText(const QString& text);

    void paint(QPainter* painter) override;

signals:
    void sessionChanged();
    void fontChanged();
    void recordingChanged();
    void titleChanged();
    void channelAttached(int cid);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void attachChannel(int cid);
    void pumpIncoming();
    void updateCellMetrics();
    int effectiveCols() const;
    int effectiveRows() const;
    QPointF cellToPixel(int row, int col, int scrollOffset) const;
    void pixelToCell(const QPointF& p, int* row, int* col) const;
    QString selectedText() const;
    void writeOut(const QByteArray& bytes);
    void appendRecording(const QString& text);

    SshSession* m_session = nullptr;
    std::shared_ptr<VtEmulator> m_vt;
    int m_cid = -1;
    QString m_fontFamily = QStringLiteral("monospace");
    int m_fontSize = 12;
    QFont m_font;
    qreal m_cellWidth = 9;
    qreal m_cellHeight = 18;
    int m_scrollOffset = 0; // 0 = live; N = N lines up
    QTimer* m_blinkTimer = nullptr;
    bool m_cursorBlinkOn = true;

    // selection (in live-screen coordinates + scrollback anchor)
    bool m_selecting = false;
    int m_selStartRow = 0, m_selStartCol = 0;
    int m_selEndRow = 0, m_selEndCol = 0;
    bool m_hasSelection = false;

    bool m_recording = false;
    QFile m_recordingFile;
    QDateTime m_recordingStart;
};

} // namespace eclipse
