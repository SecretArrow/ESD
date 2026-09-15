#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QTcpSocket;

namespace eclipse {

// ---------------------------------------------------------------------------
// TelnetSession - minimal RFC 854/855 telnet client over QTcpSocket.
//
// Used for the "New console (Telnet)" flow. It is deliberately NOT an
// SshSession subclass; it mirrors the SshSession property names QML binds
// (sessionId / title / state) and implements the console attachment contract
// consumed by the terminal page:
//    signal  void bytesReceived(const QByteArray& data)   // clean payload
//    slot    void write(const QByteArray& data)
//    slot    void ptyResize(int cols, int rows)
//
// Negotiation policy (kept simple and dumb-terminal friendly):
//   * server WILL ECHO / SGA  -> reply DO;  server DO ECHO / SGA -> reply WILL
//   * every other WILL/DO     -> reply DONT/WONT
//   * WONT/DONT and subnegotiations (SB..SE) are parsed and discarded
//   * IAC IAC escaping is handled both ways (literal 0xFF bytes)
// ---------------------------------------------------------------------------
class TelnetSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 sessionId READ sessionId CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)

public:
    explicit TelnetSession(QObject* parent = nullptr);
    ~TelnetSession() override;

    qint64 sessionId() const { return m_sessionId; }
    QString title() const { return m_title; }
    QString state() const { return m_state; }

    // profileMap keys (superset; unknown keys are ignored):
    //   host (string), port (int, default 23), name (string)
    Q_INVOKABLE void connectToProfile(const QVariantMap& profileMap);
    Q_INVOKABLE void disconnect();

public slots:
    // Console attachment contract (mirrors the SshSession data path used by
    // the terminal page); 0xFF bytes are IAC-escaped on the wire.
    void write(const QByteArray& data);
    // Telnet has no window-size tracking unless NAWS is negotiated; the
    // default policy ignores resizes (kept for the console contract).
    void ptyResize(int cols, int rows) { Q_UNUSED(cols); Q_UNUSED(rows); }

signals:
    void bytesReceived(const QByteArray& data);
    void stateChanged(const QString& state);
    void titleChanged(const QString& title);
    void errorOccurred(const QString& message);

private slots:
    void onSocketConnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError code);

private:
    enum ParserState { Data, SawIac, SawCommand, SawSubneg, SawSubnegIac };

    void setState(const QString& state);
    void setError(const QString& message);
    void feed(const QByteArray& chunk);       // negotiation parser -> bytesReceived
    void respond(quint8 command, quint8 option);
    void sendIacEscaped(const QByteArray& data);

    static qint64 s_nextId;

    qint64 m_sessionId = 0;
    QTcpSocket* m_socket = nullptr;
    QString m_title;
    QString m_state = QStringLiteral("disconnected");
    QString m_lastError;

    // Telnet parser state (persists across readyRead chunks)
    ParserState m_parserState = Data;
    quint8 m_command = 0;
    quint8 m_option = 0;
};

} // namespace eclipse
