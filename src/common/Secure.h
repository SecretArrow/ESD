#pragma once

#include <QByteArray>
#include <QString>
#include <cstring>

namespace eclipse {

// ---------------------------------------------------------------------------
// Secure memory helpers. Used wherever credentials / key material live in RAM.
// ---------------------------------------------------------------------------

inline void secureZero(void* ptr, size_t len)
{
    if (!ptr || len == 0)
        return;
#if defined(Q_OS_WIN)
    SecureZeroMemory(ptr, len);
#else
    volatile unsigned char* p = reinterpret_cast<volatile unsigned char*>(ptr);
    while (len--) {
        *p++ = 0;
    }
#endif
}

// QByteArray that wipes its contents when destroyed or reassigned.
// Copies are allowed (they also become wiping buffers) but cheap checks
// discourage accidental retention.
class SecureBuffer
{
public:
    SecureBuffer() = default;
    explicit SecureBuffer(const QByteArray& data) : m_data(data) {}
    explicit SecureBuffer(const QString& s) : m_data(s.toUtf8()) {}
    ~SecureBuffer() { wipe(); }

    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;
    SecureBuffer(SecureBuffer&& other) noexcept : m_data(std::move(other.m_data)) { other.wipe(); }
    SecureBuffer& operator=(SecureBuffer&& other) noexcept
    {
        if (this != &other) {
            wipe();
            m_data = std::move(other.m_data);
            other.wipe();
        }
        return *this;
    }

    const char* constData() const { return m_data.constData(); }
    QByteArray bytes() const { return m_data; }
    QString toQString() const { return QString::fromUtf8(m_data); }
    int size() const { return m_data.size(); }
    bool isEmpty() const { return m_data.isEmpty(); }

private:
    void wipe()
    {
        if (!m_data.isEmpty())
            secureZero(m_data.data(), size_t(m_data.size()));
        m_data.clear();
    }
    QByteArray m_data;
};

} // namespace eclipse
