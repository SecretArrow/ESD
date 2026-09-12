#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QIcon>
#include <QLockFile>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSystemTrayIcon>

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
#include "terminal/TermItem.h"
#include "ssh/Types.h"
#include "ssh/SshSession.h"
#include "ssh/ForwardManager.h"

using namespace eclipse;

static QIcon makeAppIcon()
{
    // Simple generated mark: dark rounded square with a crescent accent.
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

    // Single instance
    const QString lockPath = utils::cacheDirectory() + QStringLiteral("/single-instance.lock");
    QDir().mkpath(utils::cacheDirectory());
    QLockFile lock(lockPath);
    if (!lock.tryLock(100)) {
        LOG_APP(QStringLiteral("Another instance is already running - exiting."));
        return 0;
    }

    qRegisterMetaType<HostKeyInfo>("HostKeyInfo");
    qRegisterMetaType<EngineInfo>("EngineInfo");
    qRegisterMetaType<ForwardRuleSpec>("ForwardRuleSpec");
    qRegisterMetaType<SftpEntry>("SftpEntry");

    Logger::instance().enableFileLogging(true);
    LOG_APP(QStringLiteral("Eclipse SSH Desktop %1 starting").arg(ECLIPSE_VERSION));

    AppController::create(nullptr, nullptr);
    AppController& app_ = AppController::instance();

    QQmlApplicationEngine engine;

    // qt_add_qml_module's default RESOURCE_PREFIX is "/qt/qml" on Qt >= 6.5 and
    // "/" on older Qt 6.x. The engine only auto-searches qrc:/qt/qml since 6.5,
    // so register both layouts explicitly; the non-existing one is ignored.
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.addImportPath(QStringLiteral("qrc:/"));

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
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [url](QObject* obj, const QUrl& objUrl) {
                         if (!obj && url == objUrl)
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

    return app.exec();
}
