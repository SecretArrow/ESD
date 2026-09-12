#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QVariant>

namespace eclipse {

// Keyboard shortcut registry: action id -> sequence, with user overrides.
// Every built-in action registers here; the Shortcuts settings page edits
// overrides; QML binds via sequenceFor() and reacts to shortcutsChanged().
class ShortcutManager : public QObject
{
    Q_OBJECT
public:
    struct ActionInfo
    {
        QString id;
        QString title;
        QString category;
        QKeySequence defaultSequence;
    };

    static ShortcutManager& instance();

    void registerAction(const QString& id, const QString& title, const QString& category,
                        const QKeySequence& defaultSequence);
    // PortableText reads best on the Windows/Linux targets and round-trips
    // through QML Shortcut.sequence (which parses PortableText strings).
    Q_INVOKABLE QString sequenceFor(const QString& actionId) const;
    QString titleFor(const QString& actionId) const;
    // One QVariantMap per action: { id, title, sequence, category }.
    Q_INVOKABLE QVariantList allActions() const;
    void setOverride(const QString& actionId, const QKeySequence& seq); // empty clears
    void resetAll();

signals:
    void shortcutsChanged();

private:
    ShortcutManager();
    void loadOverrides();
    void saveOverrides();
    void registerDefaults();

    QHash<QString, ActionInfo> m_actions;
    QHash<QString, QString> m_overrides; // actionId -> sequence string
};

} // namespace eclipse
