#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <cstdio>
#include <QQuickStyle>
#include <QSystemTrayIcon>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <QWinEventNotifier>
#endif

#include "EclipseVersion.h"
#include "common/Utils.h"
#include <QDir>
#include "core/commands/CommandRegistry.h"
#include "core/logging/Logger.h"
#include "core/settings/Settings.h"
#include "core/snippets/SnippetStore.h"
#include "app/AppController.h"
#include "app/TrayController.h"
#include "transfer/TransferManager.h"
#include "sftp/RemoteFsModel.h"
#include "terminal/TermItem.h"
#include "ssh/Types.h"
#include "ssh/SshSession.h"
#include "ssh/ForwardManager.h"

using namespace eclipse;

// QML errors captured during startup. A GUI-subsystem executable has no
// console on Windows (stderr is invisible), so if the UI fails to load these
// are surfaced in a native message box instead of being lost.
static QStringList g_startupQmlErrors;

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Single instance via a named kernel mutex.
//
// The previous QLockFile implementation could permanently block startup: the
// lock file survives crashes, and its stale-lock detection fails on Windows
// when the recorded PID was reused by another (or an elevated, unopenable)
// process - the app then exited silently with code 0 on EVERY launch. A mutex
// object dies with the owning process, so no stale state can ever remain.
// ---------------------------------------------------------------------------
static HANDLE g_instanceMutex = nullptr;
static constexpr wchar_t kMutexName[] = L"Local\\EclipseSSHDesktop.SingleInstance";
static constexpr wchar_t kShowUpEventName[] = L"Local\\EclipseSSHDesktop.ShowUp";
#endif

// Attach the parent console so `--help`, `--version` and stderr diagnostics
// are visible when launched from a terminal. Skipped when stdout is already
// redirected (pipes/files keep working) and when there is no parent console
// (normal double-click launch from Explorer).
#ifdef _WIN32
static void attachParentConsole()
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const DWORD ty = out != INVALID_HANDLE_VALUE ? GetFileType(out) : FILE_TYPE_UNKNOWN;
    if (ty == FILE_TYPE_PIPE || ty == FILE_TYPE_DISK)
        return;                       // redirected - do not touch the handles
    if (GetConsoleWindow() != nullptr)
        return;                       // already attached to a console
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;                       // no parent console (Explorer launch)
    freopen("CONOUT$", "wb", stdout);
    freopen("CONOUT$", "wb", stderr);
}
#endif

static QIcon makeAppIcon()
{
    // Real eclipse icon embedded as a Qt resource (same artwork as the exe's
    // Windows icon resource); fall back to a drawn mark if it is missing.
    QIcon icon(QStringLiteral(":/appicon.png"));
    if (!icon.isNull())
        return icon;
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0x14, 0x17, 0x1e));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(2, 2, 60, 60, 14, 14);
    p.setBrush(QColor(0x5b, 0x8d, 0xef));
    p.drawEllipse(16, 12, 32, 32);
    p.setBrush(QColor(0x14, 0x17, 0x1e));
    p.drawEllipse(24, 8, 32, 32);
    p.end();
    return QIcon(pm);
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
    attachParentConsole();
#endif
    // Consistent desktop style + themable palette
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
    qputenv("QSG_RENDER_LOOP", "basic");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("EclipseSSH"));
    QApplication::setApplicationName(QStringLiteral("Eclipse SSH Desktop"));
    QApplication::setApplicationVersion(QStringLiteral(ECLIPSE_VERSION));
    app.setWindowIcon(makeAppIcon());
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Eclipse SSH Desktop - modern SSH/SFTP client"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    // File logging must be live BEFORE the single-instance check so that an
    // "already running" event is actually recorded somewhere visible.
    Logger::instance().enableFileLogging(true);

#ifdef _WIN32
    g_instanceMutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (g_instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        // Second launch: poke the running instance to raise its window and quit.
        LOG_APP(QStringLiteral("Another instance is already running - asking it to raise its window."));
        std::fprintf(stderr, "Eclipse SSH Desktop is already running.\n");
        HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, kShowUpEventName);
        if (ev) {
            SetEvent(ev);
            CloseHandle(ev);
        }
        return 0;
    }
#else
    // Single instance (POSIX): QLockFile can survive a crash, so force-remove
    // a provably stale lock and retry before giving up.
    const QString lockPath = utils::cacheDirectory() + QStringLiteral("/single-instance.lock");
    QDir().mkpath(utils::cacheDirectory());
    QLockFile lock(lockPath);
    if (!lock.tryLock(100)) {
        lock.removeStaleLockFile();
        if (!lock.tryLock(1000)) {
            LOG_APP(QStringLiteral("Another instance is already running - exiting."));
            std::fprintf(stderr, "Eclipse SSH Desktop is already running.\n");
            return 0;
        }
    }
#endif

    LOG_APP(QStringLiteral("Eclipse SSH Desktop %1 starting").arg(ECLIPSE_VERSION));

    qRegisterMetaType<HostKeyInfo>("HostKeyInfo");
    qRegisterMetaType<EngineInfo>("EngineInfo");
    qRegisterMetaType<ForwardRuleSpec>("ForwardRuleSpec");
    qRegisterMetaType<SftpEntry>("SftpEntry");

    AppController::create(nullptr, nullptr);
    AppController& app_ = AppController::instance();

#ifdef _WIN32
    // Watch the show-up event: a second launch of the exe signals it to raise
    // the main window (replaces the old silent second-instance exit).
    if (HANDLE showUpEvent = CreateEventW(nullptr, FALSE, FALSE, kShowUpEventName)) {
        auto* raiseNotifier = new QWinEventNotifier(showUpEvent, &app);
        QObject::connect(raiseNotifier, &QWinEventNotifier::activated, &app, []() {
            QMetaObject::invokeMethod(&AppController::instance(), "requestUiRaise",
                                      Qt::QueuedConnection);
        });
    }
#endif

    QQmlApplicationEngine engine;

    // Surface QML warnings on stderr and keep them for the failure dialog.
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError>& qmlWarnings) {
                         for (const QQmlError& e : qmlWarnings) {
                             std::fprintf(stderr, "QML: %s\n", qPrintable(e.toString()));
                             g_startupQmlErrors << e.toString();
                         }
                         std::fflush(stderr);
                     });

    // qt_add_qml_module's default RESOURCE_PREFIX is "/qt/qml" on Qt >= 6.5 and
    // "/" on older Qt 6.x. The engine only auto-searches qrc:/qt/qml since 6.5,
    // so register both layouts explicitly; the non-existing one is ignored.
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.addImportPath(QStringLiteral("qrc:/"));
    // Belt-and-braces: also look for QML modules next to the executable
    // (deployed layout win64\qml), independent of qt.conf processing.
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));

    // Context properties
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &app_);
    engine.rootContext()->setContextProperty(QStringLiteral("ThemeBridge"), &ThemeManager::instance());
    engine.rootContext()->setContextProperty(QStringLiteral("Transfers"), &TransferManager::instance());
    engine.rootContext()->setContextProperty(QStringLiteral("Snippets"), &SnippetStore::instance());
    engine.rootContext()->setContextProperty(QStringLiteral("Shortcuts"), &ShortcutManager::instance());
    engine.rootContext()->setContextProperty(QStringLiteral("Logs"), Logger::instance().uiModel());
    engine.rootContext()->setContextProperty(QStringLiteral("trayIcon"), new TrayController(&app_));

    qmlRegisterType<TransferModel>("Eclipse.Internal", 1, 0, "TransferModel");
    qmlRegisterType<TermItem>("Eclipse.Internal", 1, 0, "TermItem");
    qmlRegisterType<SnippetModel>("Eclipse.Internal", 1, 0, "SnippetModel");
    qmlRegisterType<TrayController>("Eclipse.Internal", 1, 0, "QTrayIcon");
    qmlRegisterType<RemoteFsModel>("Eclipse.Internal", 1, 0, "RemoteFsModel");
    qmlRegisterType<LocalFsModel>("Eclipse.Internal", 1, 0, "LocalFsModel");

    // Pick the Main.qml URL that exists in this build's resources (QFile handles
    // the qrc: scheme; QUrl::toLocalFile() would return an empty string here).
    QUrl url(QStringLiteral("qrc:/qt/qml/Eclipse/qml/Main.qml"));
    for (const char* candidate : { "qrc:/qt/qml/Eclipse/qml/Main.qml",
                                   "qrc:/Eclipse/qml/Main.qml" }) {
        if (QFile::exists(QString::fromLatin1(candidate))) {
            url = QUrl(QString::fromLatin1(candidate));
            break;
        }
    }
    std::fprintf(stderr, "QML root: %s\n", qPrintable(url.toString()));
    std::fflush(stderr);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [url](QObject* obj, const QUrl& objUrl) {
                         if (obj || url != objUrl)
                             return;
                         LOG_APP(QStringLiteral("UI failed to load - exiting."));
                         // GUI-subsystem binaries have no console: surface the
                         // failure in a native dialog (skipped for headless runs
                         // so CI never blocks on a dialog).
                         const bool headless =
                             QGuiApplication::platformName() == QLatin1String("offscreen") ||
                             qEnvironmentVariableIsSet("ECLIPSE_HEADLESS");
                         if (!headless) {
                             QString details = g_startupQmlErrors.join(QLatin1String("\n"));
                             if (details.isEmpty())
                                 details = QStringLiteral(
                                     "No QML errors were reported; see the log file for details.");
                             QMessageBox box(
                                 QMessageBox::Critical,
                                 QStringLiteral("Eclipse SSH Desktop could not start"),
                                 QStringLiteral("The user interface failed to load and the application has to exit.\n\n%1\n\nLog file:\n%2")
                                     .arg(details, Logger::instance().logFilePath()),
                                 QMessageBox::Ok);
                             box.exec();
                         }
                         QCoreApplication::exit(-1);
                     },
                     Qt::QueuedConnection);
    engine.load(url);

    // ---- Global command palette entries -----------------------------------
    AppController& controller = AppController::instance();
    auto reg = [&controller](const QString& id, const QString& title, const QString& cat,
                             const QString& kw) {
        CommandRegistry::instance().registerCommand(
            { id, title, cat, kw, [&controller, id] { emit controller.commandRequested(id); }, true });
    };
    reg("app.new-connection", "New Connection", "Connections", "create profile server");
    reg("app.quick-connect", "Quick Connect", "Connections", "fast ad-hoc");
    reg("app.new-terminal", "New Terminal", "Terminal", "shell session");
    reg("app.open-sftp", "Open File Manager", "Files", "sftp two panel");
    reg("app.settings", "Open Settings", "General", "preferences options");
    reg("app.toggle-dark", "Toggle Dark Mode", "Appearance", "theme light dark");
    reg("app.logs", "View Logs", "General", "debug output viewer");
    reg("app.snippets", "Snippets", "General", "commands templates variables");
    reg("app.command-runner", "Command Runner", "General", "multi server execution");
    reg("app.forwarding", "Port Forwarding", "Tunnel", "local remote dynamic socks");
    reg("app.diagnostics", "Connection Diagnostics", "General", "troubleshoot dns tcp");
    reg("app.import-openssh", "Import OpenSSH Config", "Connections", "known hosts ssh config");
    reg("app.export-profiles", "Export Profiles", "Connections", "backup json");
    reg("app.about", "About Eclipse SSH Desktop", "General", "version info");

    const int rc = app.exec();
#ifdef _WIN32
    if (g_instanceMutex) {
        ReleaseMutex(g_instanceMutex);
        CloseHandle(g_instanceMutex);
        g_instanceMutex = nullptr;
    }
#endif
    return rc;
}
