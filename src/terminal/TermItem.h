#pragma once

#include <QQuickPaintedItem>
#include <QFile>
#include <QImage>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <utility>

#include "../terminal/VtEmulator.h"
#include "../ssh/SshSession.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// TermItem - QML terminal renderer (QQuickPaintedItem) bound to one channel of
// an SshSession. Features: full color (16/256/truecolor), bold/italic/
// underline/strike/reverse, scrollback with wheel scrolling, selection +
// copy, bracketed paste, mouse reporting pass-through, blinking cursor,
// resize (pty change), alternate screen (via libvterm), find-in-buffer with
// highlight + navigation, OSC 133 shell integration signals/marker painting
// and inline sixel previews (Task 2-a).
// ---------------------------------------------------------------------------
class TermItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(SshSession* session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    Q_PROPERTY(bool recording READ recording WRITE setRecording NOTIFY recordingChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)

    // find-in-buffer (Task 2-a)
    Q_PROPERTY(QString searchTerm READ searchTerm WRITE setSearchTerm NOTIFY searchTermChanged)
    Q_PROPERTY(bool searchCaseSensitive READ searchCaseSensitive WRITE setSearchCaseSensitive
               NOTIFY searchCaseSensitiveChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY matchInfoChanged)
    Q_PROPERTY(int currentMatch READ currentMatch NOTIFY matchInfoChanged)

    // OSC 133 shell integration (Task 2-a)
    Q_PROPERTY(bool shellIntegrationActive READ shellIntegrationActive NOTIFY shellIntegrationChanged)

    // sixel previews (Task 2-a; in-buffer rendering is deferred)
    Q_PROPERTY(QVariantList sixelThumbnails READ sixelThumbnails NOTIFY sixelThumbnailsChanged)

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

    QString searchTerm() const { return m_searchTerm; }
    void setSearchTerm(const QString& term);
    bool searchCaseSensitive() const { return m_searchCaseSensitive; }
    void setSearchCaseSensitive(bool on);
    int matchCount() const { return m_matches.size(); }
    int currentMatch() const { return m_currentMatch; }
    bool shellIntegrationActive() const;
    QVariantList sixelThumbnails() const { return m_sixelThumbnails; }

    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void pasteClipboard();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearScrollback();
    Q_INVOKABLE void scrollToBottom();
    Q_INVOKABLE void sendText(const QString& text);

    // find-in-buffer (Task 2-a)
    Q_INVOKABLE void findNext();
    Q_INVOKABLE void findPrevious();
    Q_INVOKABLE void endSearch();

    // OSC 133 shell integration (Task 2-a)
    Q_INVOKABLE void jumpToPreviousCommand();
    Q_INVOKABLE void jumpToNextCommand();

    // sixel previews (Task 2-a)
    Q_INVOKABLE void clearSixelThumbnails();

    void paint(QPainter* painter) override;

signals:
    void sessionChanged();
    void fontChanged();
    void recordingChanged();
    void titleChanged();
    void channelAttached(int cid);

    // find-in-buffer (Task 2-a)
    void searchTermChanged();
    void searchCaseSensitiveChanged();
    void matchInfoChanged();

    // OSC 133 shell integration (Task 2-a; aliased from VtEmulator)
    void commandStarted();
    void commandFinished(int exitCode);
    void shellIntegrationChanged();

    // sixel (Task 2-a)
    void sixelImageReady(const QImage& image);
    void sixelThumbnailsChanged();

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

    // Task 2-a helpers
    void rerunSearch(bool jumpToFirst);
    void scrollToMatchRow(int absoluteRow);
    void addSixelThumbnail(const QImage& image);

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

    // find-in-buffer (Task 2-a)
    QString m_searchTerm;
    bool m_searchCaseSensitive = false;
    QVector<QPair<int, int>> m_matches; // (absoluteRow, col), row-ascending
    int m_currentMatch = -1;
    QTimer* m_searchRefreshTimer = nullptr; // re-run search after new output

    // sixel previews (Task 2-a)
    QVariantList m_sixelThumbnails; // file:// URLs for the QML preview strip
};

} // namespace eclipse
