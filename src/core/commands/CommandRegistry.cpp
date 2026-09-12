#include "CommandRegistry.h"

#include <algorithm>

namespace eclipse {

CommandRegistry& CommandRegistry::instance()
{
    static CommandRegistry r;
    return r;
}

void CommandRegistry::registerCommand(const Command& cmd)
{
    unregister(cmd.id);
    m_commands.append(cmd);
    emit registryChanged();
}

void CommandRegistry::unregister(const QString& id)
{
    m_commands.removeIf([&](const Command& c) { return c.id == id; });
    emit registryChanged();
}

QList<CommandRegistry::Command> CommandRegistry::commands() const
{
    return m_commands;
}

CommandRegistry::Command CommandRegistry::find(const QString& id) const
{
    for (const auto& c : m_commands)
        if (c.id == id)
            return c;
    return {};
}

// Simple subsequence scoring: higher is better. Returns -1 when no match.
int CommandRegistry::fuzzyScore(const QString& text, const QString& query)
{
    if (query.isEmpty())
        return 1;
    int score = 0, ti = 0, streak = 0;
    const QString t = text.toLower();
    const QString q = query.toLower();
    for (const QChar qc : q) {
        bool found = false;
        while (ti < t.size()) {
            if (t[ti] == qc) {
                found = true;
                ++streak;
                score += 2 + streak;
                ++ti;
                break;
            }
            ++ti;
            streak = 0;
        }
        if (!found)
            return -1;
    }
    if (t.startsWith(q))
        score += 20;
    if (t.contains(q))
        score += 10;
    return score;
}

QList<CommandRegistry::Command> CommandRegistry::query(const QString& text) const
{
    QList<QPair<int, Command>> scored;
    for (const auto& c : m_commands) {
        if (!c.enabled)
            continue;
        const int s1 = fuzzyScore(c.title, text);
        const int s2 = fuzzyScore(c.keywords, text) / 2;
        const int s3 = fuzzyScore(c.category, text) / 3;
        const int best = std::max({ s1, s2, s3 });
        if (best >= 0)
            scored.append({ best, c });
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    QList<Command> out;
    out.reserve(scored.size());
    for (const auto& s : scored)
        out.append(s.second);
    return out;
}

} // namespace eclipse
