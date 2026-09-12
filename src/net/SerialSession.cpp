#include "SerialSession.h"

#ifdef ECLIPSE_HAVE_SERIALPORT

#include "../core/logging/Logger.h"

namespace eclipse {

qint64 SerialSession::s_nextId = 1;

SerialSession::SerialSession(QObject* parent)
    : QObject(parent)
    , m_sessionId(s_nextId++)
{
    m_port = new QSerialPort(this);
    connect(m_port, &QSerialPort::readyRead, this, [this]() {
        const QByteArray data = m_port->readAll();
        if (!data.isEmpty())
            emit bytesReceived(data);
    });
    connect(m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError code) {
        // ResourceError on unplug / DeviceNotFoundError on open failures are
        // real errors; "NoError" and "UnknownError" noise is filtered by
        // checking the error string emptiness below.
        if (code == QSerialPort::NoError)
            return;
        if (code == QSerialPort::PermissionError || code == QSerialPort::DeviceNotFoundError
            || code == QSerialPort::ResourceError || code == QSerialPort::OpenError) {
            setError(QStringLiteral("Serial error: %1").arg(m_port->errorString()));
            return;
        }
        if (m_state == QLatin1String("connected"))
            setError(QStringLiteral("Serial error: %1").arg(m_port->errorString()));
    });
    connect(m_port, &QSerialPort::aboutToClose, this, [this]() {
        if (m_state != QLatin1String("error"))
            setState(QStringLiteral("disconnected"));
    });
}

SerialSession::~SerialSession()
{
    if (m_port && m_port->isOpen())
        m_port->close();
}

void SerialSession::setState(const QString& state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void SerialSession::setError(const QString& message)
{
    m_lastError = message;
    LOG_CONN_ERR(QStringLiteral("Serial %1: %2").arg(m_title, message));
    emit errorOccurred(message);
    setState(QStringLiteral("error"));
}

void SerialSession::connectToProfile(const QVariantMap& profileMap)
{
    if (m_port->isOpen())
        m_port->close();
    m_lastError.clear();

    m_title = profileMap.value(QStringLiteral("name")).toString();
    if (m_title.isEmpty())
        m_title = profileMap.value(QStringLiteral("serialPort")).toString();
    emit titleChanged(m_title);

    const QString device = profileMap.value(QStringLiteral("serialPort")).toString().trimmed();
    if (device.isEmpty()) {
        setError(QStringLiteral("No serial device given for the console."));
        return;
    }

    m_port->setPortName(device);
    m_port->setBaudRate(qint32(profileMap.value(QStringLiteral("serialBaud"), 115200).toInt()));

    const int dataBits = profileMap.value(QStringLiteral("serialDataBits"), 8).toInt();
    m_port->setDataBits(dataBits == 7 ? QSerialPort::Data7 : QSerialPort::Data8);

    const QString parity = profileMap.value(QStringLiteral("serialParity"), QStringLiteral("none")).toString();
    if (parity.compare(QLatin1String("even"), Qt::CaseInsensitive) == 0)
        m_port->setParity(QSerialPort::EvenParity);
    else if (parity.compare(QLatin1String("odd"), Qt::CaseInsensitive) == 0)
        m_port->setParity(QSerialPort::OddParity);
    else
        m_port->setParity(QSerialPort::NoParity);

    const int stopBits = profileMap.value(QStringLiteral("serialStopBits"), 1).toInt();
    m_port->setStopBits(stopBits == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop);

    const int flow = profileMap.value(QStringLiteral("serialFlowControl"), 0).toInt();
    if (flow == 1)
        m_port->setFlowControl(QSerialPort::HardwareControl);   // rtscts
    else if (flow == 2)
        m_port->setFlowControl(QSerialPort::SoftwareControl);   // xonxoff
    else
        m_port->setFlowControl(QSerialPort::NoFlowControl);

    setState(QStringLiteral("connecting"));
    LOG_CONN(QStringLiteral("Serial opening %1 (%2 %3%4%5)").arg(
                 device, QString::number(m_port->baudRate()),
                 QString::number(int(m_port->dataBits())),
                 parity.left(1).toUpper(),
                 QString::number(stopBits)));

    if (!m_port->open(QIODevice::ReadWrite)) {
        setError(QStringLiteral("Cannot open %1: %2").arg(device, m_port->errorString()));
        return;
    }
    LOG_CONN(QStringLiteral("Serial connected: %1").arg(m_title));
    setState(QStringLiteral("connected"));
}

void SerialSession::disconnect()
{
    if (m_port->isOpen()) {
        m_port->close();
        setState(QStringLiteral("disconnected"));
    }
}

void SerialSession::write(const QByteArray& data)
{
    if (m_port->isOpen() && !data.isEmpty())
        m_port->write(data);
}

} // namespace eclipse

#else // stub: define the static only (API is header-only) ---------------------

namespace eclipse {
qint64 SerialSession::s_nextId = 1;
} // namespace eclipse

#endif // ECLIPSE_HAVE_SERIALPORT
