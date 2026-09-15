#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

// ---------------------------------------------------------------------------
// SerialSession - serial console session with the same interface contract as
// TelnetSession (sessionId / title / state properties, bytesReceived signal,
// write() / ptyResize() slots).
//
// The real implementation needs Qt6::SerialPort. The build system defines
// ECLIPSE_HAVE_SERIALPORT when Qt6::SerialPort is found (CMake TODO for the
// integrator: find_package(Qt6 OPTIONAL_COMPONENTS SerialPort) +
// target_compile_definitions(eclipse_app PRIVATE ECLIPSE_HAVE_SERIALPORT)).
// Without it a compiling stub with the identical API is provided that reports
// a clear error on every connect attempt.
// ---------------------------------------------------------------------------
#ifdef ECLIPSE_HAVE_SERIALPORT

#include <QSerialPort>

namespace eclipse {

class SerialSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 sessionId READ sessionId CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)

public:
    explicit SerialSession(QObject* parent = nullptr);
    ~SerialSession() override;

    qint64 sessionId() const { return m_sessionId; }
    QString title() const { return m_title; }
    QString state() const { return m_state; }

    // profileMap keys: name, serialPort ("COM3" | "/dev/ttyUSB0"),
    // serialBaud (115200), serialDataBits (7|8), serialParity
    // ("none"|"even"|"odd"), serialStopBits (1|2), serialFlowControl
    // (0 none | 1 rtscts | 2 xonxoff)
    Q_INVOKABLE void connectToProfile(const QVariantMap& profileMap);
    Q_INVOKABLE void disconnect();

public slots:
    void write(const QByteArray& data);
    void ptyResize(int cols, int rows) { Q_UNUSED(cols); Q_UNUSED(rows); }

signals:
    void bytesReceived(const QByteArray& data);
    void stateChanged(const QString& state);
    void titleChanged(const QString& title);
    void errorOccurred(const QString& message);

private:
    void setState(const QString& state);
    void setError(const QString& message);

    static qint64 s_nextId;

    qint64 m_sessionId = 0;
    QSerialPort* m_port = nullptr;
    QString m_title;
    QString m_state = QStringLiteral("disconnected");
    QString m_lastError;
};

} // namespace eclipse

#else // !ECLIPSE_HAVE_SERIALPORT - stub with the identical API -----------------

namespace eclipse {

class SerialSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 sessionId READ sessionId CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)

public:
    explicit SerialSession(QObject* parent = nullptr)
        : QObject(parent)
        , m_sessionId(s_nextId++)
    {
    }

    qint64 sessionId() const { return m_sessionId; }
    QString title() const { return m_title; }
    QString state() const { return m_state; }

    Q_INVOKABLE void connectToProfile(const QVariantMap& profileMap)
    {
        Q_UNUSED(profileMap);
        m_title = profileMap.value(QStringLiteral("serialPort")).toString();
        emit titleChanged(m_title);
        // Honest failure: the Qt6::SerialPort module was not linked in.
        emit errorOccurred(QStringLiteral(
            "Serial support not compiled in - Qt6::SerialPort missing"));
        emit stateChanged(QStringLiteral("error"));
        m_state = QStringLiteral("error");
    }

    Q_INVOKABLE void disconnect() {}

public slots:
    void write(const QByteArray& data) { Q_UNUSED(data); }
    void ptyResize(int cols, int rows) { Q_UNUSED(cols); Q_UNUSED(rows); }

signals:
    void bytesReceived(const QByteArray& data);
    void stateChanged(const QString& state);
    void titleChanged(const QString& title);
    void errorOccurred(const QString& message);

private:
    static qint64 s_nextId;

    qint64 m_sessionId = 0;
    QString m_title;
    QString m_state = QStringLiteral("disconnected");
};

} // namespace eclipse

#endif // ECLIPSE_HAVE_SERIALPORT
