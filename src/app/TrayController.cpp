#include "TrayController.h"

#include <QApplication>
#include <QMenu>
#include <QQuickWindow>

#include "../core/logging/Logger.h"
#include "../core/settings/Settings.h"
#include "AppController.h"

namespace eclipse {

TrayController::TrayController(QObject* parent)
    : QObject(parent)
{
    m_menu = new QMenu();
    m_tray = new QSystemTrayIcon(this);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger && m_window) {
                    if (m_window->property("visible").toBool()
                        && !m_window->property("minimized").toBool()) {
                        if (Settings::instance().minimizeToTray())
                            m_window->setProperty("visible", false);
                    } else {
                        m_window->setProperty("visible", true);
                        m_window->setProperty("minimized", false);
                    }
                }
            });
    connect(&AppController::instance(), &AppController::notify, this,
            [this](const QString& title, const QString& body, bool isError) {
                if (m_tray && m_tray->isVisible())
                    m_tray->showMessage(title, body,
                                        isError ? QSystemTrayIcon::Critical : QSystemTrayIcon::Information,
                                        4000);
            });
    rebuildMenu();
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray->setIcon(QApplication::windowIcon());
        m_tray->show();
        emit visibleChanged();
    }
}

TrayController::~TrayController()
{
    delete m_menu;
}

void TrayController::setWindow(QObject* w)
{
    if (m_window == w)
        return;
    m_window = w;
    emit windowChanged();
    rebuildMenu();
}

void TrayController::handleWindowClose()
{
    if (Settings::instance().minimizeToTray()) {
        if (m_window)
            m_window->setProperty("visible", false);
        LOG_APP(QStringLiteral("Window hidden to tray."));
    } else {
        AppController::instance().quitApplication();
    }
}

void TrayController::rebuildMenu()
{
    if (!m_menu)
        return;
    m_menu->clear();
    QAction* open = m_menu->addAction(tr("Open Eclipse SSH Desktop"));
    connect(open, &QAction::triggered, this, [this] {
        if (m_window) {
            m_window->setProperty("visible", true);
        }
    });
    m_menu->addSeparator();

    const auto sessions = SessionManager::instance().sessions();
    if (sessions.isEmpty()) {
        QAction* none = m_menu->addAction(tr("No active connections"));
        none->setEnabled(false);
    } else {
        for (SshSession* s : sessions) {
            QAction* a = m_menu->addAction(
                QStringLiteral("%1 (%2)").arg(s->name(), s->stateName()));
            connect(a, &QAction::triggered, this, [this, s] {
                if (m_window)
                    m_window->setProperty("visible", true);
            });
        }
    }
    m_menu->addSeparator();
    QAction* quit = m_menu->addAction(tr("Quit"));
    connect(quit, &QAction::triggered, this, [] {
        AppController::instance().quitApplication();
    });
    if (m_tray)
        m_tray->setContextMenu(m_menu);
}

} // namespace eclipse
