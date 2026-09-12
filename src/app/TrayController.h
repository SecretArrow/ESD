#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class QMenu;

namespace eclipse {

// System tray icon controller exposed to QML as QTrayIcon.
class TrayController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool visible READ trayVisible NOTIFY visibleChanged)

public:
    explicit TrayController(QObject* parent = nullptr);
    ~TrayController() override;

    QObject* window() const { return m_window; }
    void setWindow(QObject* w);
    bool trayVisible() const { return m_tray && m_tray->isVisible(); }

    Q_INVOKABLE void rebuildMenu();
    Q_INVOKABLE void handleWindowClose();

signals:
    void windowChanged();
    void visibleChanged();

private:
    QObject* m_window = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_menu = nullptr;
};

} // namespace eclipse
