#pragma once

#include <QList>
#include <QObject>
#include <functional>

namespace eclipse {

// Global command registry powering the Command Palette (Ctrl+Shift+P).
// C++ features register concrete commands; QML can register UI-scoped ones.
class CommandRegistry : public QObject
{
    Q_OBJECT
public:
    struct Command
    {
        QString id;
        QString title;
        QString category;
        QString keywords;
        std::function<void()> run;
        bool enabled = true;
    };

    static CommandRegistry& instance();

    void registerCommand(const Command& cmd);
    void unregister(const QString& id);
    QList<Command> commands() const;

    // Subsequence fuzzy match, scored; empty query returns all.
    QList<Command> query(const QString& text) const;
    Command find(const QString& id) const;

signals:
    void registryChanged();

private:
    CommandRegistry() = default;
    QList<Command> m_commands;
    static int fuzzyScore(const QString& text, const QString& query);
};

} // namespace eclipse
