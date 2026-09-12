#include "ShortcutManager.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "../settings/Settings.h"

namespace eclipse {

ShortcutManager& ShortcutManager::instance()
{
    static ShortcutManager m;
    return m;
}

ShortcutManager::ShortcutManager()
{
    registerDefaults();
    loadOverrides();
}

void ShortcutManager::registerDefaults()
{
    registerAction(QStringLiteral("app.new-connection"), QStringLiteral("New Connection"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("Ctrl+N")));
    registerAction(QStringLiteral("app.quick-connect"), QStringLiteral("Quick Connect"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    registerAction(QStringLiteral("app.new-terminal"), QStringLiteral("New Terminal"),
                   QStringLiteral("Terminal"), QKeySequence(QStringLiteral("Ctrl+T")));
    registerAction(QStringLiteral("app.new-tab"), QStringLiteral("New Tab"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    registerAction(QStringLiteral("app.close-tab"), QStringLiteral("Close Tab"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("Ctrl+W")));
    registerAction(QStringLiteral("app.command-palette"), QStringLiteral("Command Palette"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    registerAction(QStringLiteral("app.open-sftp"), QStringLiteral("Open SFTP Panel"),
                   QStringLiteral("Files"), QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    registerAction(QStringLiteral("app.search"), QStringLiteral("Search"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("Ctrl+F")));
    registerAction(QStringLiteral("app.search-files"), QStringLiteral("Search Files"),
                   QStringLiteral("Files"), QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    registerAction(QStringLiteral("app.settings"), QStringLiteral("Open Settings"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("Ctrl+,")));
    registerAction(QStringLiteral("app.logs"), QStringLiteral("View Logs"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    registerAction(QStringLiteral("app.toggle-dark"), QStringLiteral("Toggle Dark Mode"),
                   QStringLiteral("Appearance"), QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    registerAction(QStringLiteral("app.reload"), QStringLiteral("Reload"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("F5")));
    registerAction(QStringLiteral("app.rename"), QStringLiteral("Rename"),
                   QStringLiteral("Files"), QKeySequence(QStringLiteral("F2")));
    registerAction(QStringLiteral("app.delete"), QStringLiteral("Delete"),
                   QStringLiteral("Files"), QKeySequence(QStringLiteral("Del")));
    registerAction(QStringLiteral("app.copy"), QStringLiteral("Copy"),
                   QStringLiteral("Files"), QKeySequence(QStringLiteral("Ctrl+C")));
    registerAction(QStringLiteral("app.paste"), QStringLiteral("Paste"),
                   QStringLiteral("Files"), QKeySequence(QStringLiteral("Ctrl+V")));
    registerAction(QStringLiteral("app.upload"), QStringLiteral("Upload"),
                   QStringLiteral("Files"), QKeySequence(QStringLiteral("Ctrl+U")));
    registerAction(QStringLiteral("app.download"), QStringLiteral("Download"),
                   QStringLiteral("Files"), QKeySequence(QStringLiteral("Ctrl+Shift+U")));
    registerAction(QStringLiteral("app.forwarding"), QStringLiteral("Port Forwarding"),
                   QStringLiteral("Tunnel"), QKeySequence(QStringLiteral("Ctrl+Shift+F10")));
    registerAction(QStringLiteral("app.snippets"), QStringLiteral("Snippets"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    registerAction(QStringLiteral("app.command-runner"), QStringLiteral("Command Runner"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    registerAction(QStringLiteral("app.diagnostics"), QStringLiteral("Connection Diagnostics"),
                   QStringLiteral("General"), QKeySequence(QStringLiteral("Ctrl+Shift+I")));
    registerAction(QStringLiteral("term.split-right"), QStringLiteral("Split Terminal Right"),
                   QStringLiteral("Terminal"), QKeySequence(QStringLiteral("Ctrl+Shift+E, R")));
    registerAction(QStringLiteral("term.split-down"), QStringLiteral("Split Terminal Down"),
                   QStringLiteral("Terminal"), QKeySequence(QStringLiteral("Ctrl+Shift+E, D")));
    registerAction(QStringLiteral("term.copy"), QStringLiteral("Copy (Terminal)"),
                   QStringLiteral("Terminal"), QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    registerAction(QStringLiteral("term.paste"), QStringLiteral("Paste (Terminal)"),
                   QStringLiteral("Terminal"), QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    registerAction(QStringLiteral("term.clear"), QStringLiteral("Clear Terminal"),
                   QStringLiteral("Terminal"), QKeySequence(QStringLiteral("Ctrl+Shift+X")));
    registerAction(QStringLiteral("term.find"), QStringLiteral("Find in Terminal"),
                   QStringLiteral("Terminal"), QKeySequence(QStringLiteral("Ctrl+Shift+F3")));
}

void ShortcutManager::registerAction(const QString& id, const QString& title,
                                     const QString& category, const QKeySequence& defaultSequence)
{
    m_actions.insert(id, { id, title, category, defaultSequence });
}

QString ShortcutManager::sequenceFor(const QString& actionId) const
{
    const QString ov = m_overrides.value(actionId);
    if (!ov.isEmpty())
        return ov; // overrides are stored in PortableText already
    const auto it = m_actions.constFind(actionId);
    return it == m_actions.cend() ? QString()
                                  : it->defaultSequence.toString(QKeySequence::PortableText);
}

QString ShortcutManager::titleFor(const QString& actionId) const
{
    return m_actions.value(actionId).title;
}

QVariantList ShortcutManager::allActions() const
{
    QVector<ActionInfo> sorted;
    sorted.reserve(m_actions.size());
    for (const auto& v : m_actions)
        sorted.append(v);
    std::sort(sorted.begin(), sorted.end(), [](const ActionInfo& a, const ActionInfo& b) {
        return a.category < b.category || (a.category == b.category && a.title < b.title);
    });
    QVariantList out;
    out.reserve(sorted.size());
    for (const auto& a : sorted) {
        out.append(QVariantMap {
            { "id", a.id },
            { "title", a.title },
            { "sequence", sequenceFor(a.id) },
            { "category", a.category },
        });
    }
    return out;
}

void ShortcutManager::setOverride(const QString& actionId, const QKeySequence& seq)
{
    if (seq.isEmpty())
        m_overrides.remove(actionId);
    else
        m_overrides.insert(actionId, seq.toString(QKeySequence::PortableText));
    saveOverrides();
    emit shortcutsChanged();
}

void ShortcutManager::resetAll()
{
    m_overrides.clear();
    saveOverrides();
    emit shortcutsChanged();
}

void ShortcutManager::loadOverrides()
{
    const QString json = Settings::instance()
                             .value(QStringLiteral("shortcuts/overrides"))
                             .toString();
    if (json.isEmpty())
        return;
    const auto obj = QJsonDocument::fromJson(json.toUtf8()).object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        m_overrides.insert(it.key(), it.value().toString());
}

void ShortcutManager::saveOverrides()
{
    QJsonObject obj;
    for (auto it = m_overrides.begin(); it != m_overrides.end(); ++it)
        obj.insert(it.key(), it.value());
    Settings::instance().setValue(QStringLiteral("shortcuts/overrides"),
                                  QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

} // namespace eclipse
