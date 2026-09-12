#include "TelnetSession.h"

#include <QAbstractSocket>
#include <QTcpSocket>

#include "../core/logging/Logger.h"

namespace eclipse {

qint64 TelnetSession::s_nextId = 1;

namespace {

// RFC 854 telnet commands / options used by the parser.
constexpr quint8 kIac = 255;
constexpr quint8 kDont = 254;
constexpr quint8 kDo = 253;
constexpr quint8 kWont = 252;
constexpr quint8 kWill = 251;
constexpr quint8 kSb = 250;
constexpr quint8 kSe = 240;

constexpr quint8 kOptEcho = 1;  // RFC 857
constexpr quint8 kOptSga = 3;   // RFC 858 suppress-go-ahead

} // namespace

TelnetSession::TelnetSession(QObject* parent)
    : QObject(parent)
    , m_sessionId(s_nextId++)
{
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &TelnetSession::onSocketConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TelnetSession::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
        if (m_state != QLatin1String("error"))
            setState(QStringLiteral("disconnected"));
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this, &TelnetSession::onSocketError);
}

TelnetSession::~TelnetSession()
{
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
}

void TelnetSession::setState(const QString& state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void TelnetSession::setError(const QString& message)
{
    if (!m_lastError.isEmpty() && m_state == QLatin1String("error"))
        return; // keep the first error
    m_lastError = message;
    LOG_CONN_ERR(QStringLiteral("Telnet %1: %2").arg(m_title, message));
    emit errorOccurred(message);
    setState(QStringLiteral("error"));
}

void TelnetSession::connectToProfile(const QVariantMap& profileMap)
{
    // Reconnect on a fresh socket keeps state handling simple.
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    m_lastError.clear();
    m_parserState = Data;

    m_title = profileMap.value(QStringLiteral("name")).toString();
    const QString host = profileMap.value(QStringLiteral("host")).toString().trimmed();
    int port = profileMap.value(QStringLiteral("port")).toInt();
    if (port <= 0)
        port = 23; // RFC 854 well-known port
    if (m_title.isEmpty())
        m_title = QStringLiteral("%1:%2").arg(host, QString::number(port));
    emit titleChanged(m_title);

    if (host.isEmpty()) {
        setError(QStringLiteral("No host given for the Telnet console."));
        return;
    }

    setState(QStringLiteral("connecting"));
    LOG_CONN(QStringLiteral("Telnet connecting to %1:%2").arg(host).arg(port));
    m_socket->connectToHost(host, quint16(port));
}

void TelnetSession::disconnect()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
        setState(QStringLiteral("disconnected"));
    }
}

void TelnetSession::onSocketConnected()
{
    LOG_CONN(QStringLiteral("Telnet connected: %1").arg(m_title));
    setState(QStringLiteral("connected"));
}

void TelnetSession::onSocketError(QAbstractSocket::SocketError code)
{
    // A remote close on an established session is a normal disconnect, not an
    // error worth switching to the "error" state.
    if (code == QAbstractSocket::RemoteHostClosedError && m_state == QLatin1String("connected")) {
        setState(QStringLiteral("disconnected"));
        return;
    }
    setError(QStringLiteral("Telnet error: %1").arg(m_socket->errorString()));
}

void TelnetSession::onReadyRead()
{
    feed(m_socket->readAll());
}

// ---------------------------------------------------------------------------
// Negotiation parser
// ---------------------------------------------------------------------------
void TelnetSession::feed(const QByteArray& chunk)
{
    QByteArray clean;
    clean.reserve(chunk.size());
    for (char raw : chunk) {
        const quint8 c = quint8(raw);
        switch (m_parserState) {
        case Data:
            if (c == kIac)
                m_parserState = SawIac;
            else
                clean.append(raw);
            break;
        case SawIac:
            if (c == kIac) {          // IAC IAC -> literal 0xFF
                clean.append(raw);
                m_parserState = Data;
            } else if (c == kWill || c == kWont || c == kDo || c == kDont) {
                m_command = c;
                m_parserState = SawCommand;
            } else if (c == kSb) {
                m_parserState = SawSubneg;
            } else {
                m_parserState = Data; // other commands (NOP, AYT, ...) ignored
            }
            break;
        case SawCommand:
            m_option = c;
            if (m_command == kWill) {
                // Feel right in dumb terminals: accept ECHO + SGA, refuse rest.
                respond(m_option == kOptEcho || m_option == kOptSga ? kDo : kDont, m_option);
            } else if (m_command == kDo) {
                respond(m_option == kOptEcho || m_option == kOptSga ? kWill : kWont, m_option);
            }
            // WONT/DONT need no reply (RFC 854: only acknowledge WILL/DO).
            m_parserState = Data;
            break;
        case SawSubneg:
            if (c == kIac)
                m_parserState = SawSubnegIac;
            break;
        case SawSubnegIac:
            if (c == kSe)
                m_parserState = Data;   // end of subnegotiation (content discarded)
            else
                m_parserState = SawSubneg; // escaped 0xFF inside SB..SE
            break;
        }
    }
    if (!clean.isEmpty())
        emit bytesReceived(clean);
}

void TelnetSession::respond(quint8 command, quint8 option)
{
    const quint8 reply[3] = { kIac, command, option };
    m_socket->write(reinterpret_cast<const char*>(reply), 3);
}

void TelnetSession::write(const QByteArray& data)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState || data.isEmpty())
        return;
    sendIacEscaped(data);
}

void TelnetSession::sendIacEscaped(const QByteArray& data)
{
    if (!data.contains(char(kIac))) {
        m_socket->write(data);
        return;
    }
    QByteArray escaped;
    escaped.reserve(int(data.size() * 1.2));
    for (char c : data) {
        escaped.append(c);
        if (quint8(c) == kIac)
            escaped.append(char(kIac)); // 0xFF -> IAC IAC on the wire
    }
    m_socket->write(escaped);
}

} // namespace eclipse
