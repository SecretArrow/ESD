#include <QtTest/QtTest>

#include "../src/archive/Archive.h"

#include <QTemporaryDir>
#include <QFile>
#include <zlib.h>

using namespace eclipse;

namespace {

// Builds a small in-memory ZIP (store + deflate) for round-trip tests.
QByteArray makeTestZip()
{
    struct Entry
    {
        QByteArray name;
        QByteArray data;
        bool deflate;
    };
    const QVector<Entry> entries = {
        { QByteArrayLiteral("hello.txt"), QByteArrayLiteral("Hello from Eclipse SSH Desktop!"), false },
        { QByteArrayLiteral("src/main.cpp"), QByteArrayLiteral("int main() { return 0; }\n"), true },
        { QByteArrayLiteral("docs/"), QByteArray(), false },
    };

    QByteArray out;
    QVector<quint32> offsets;
    QVector<quint32> crcs;
    QVector<quint16> methods;
    QVector<quint32> m_sizes;

    auto w16 = [&out](quint16 v) {
        out.append(char(v & 0xFF));
        out.append(char((v >> 8) & 0xFF));
    };
    auto w32 = [&out](quint32 v) {
        out.append(char(v & 0xFF));
        out.append(char((v >> 8) & 0xFF));
        out.append(char((v >> 16) & 0xFF));
        out.append(char((v >> 24) & 0xFF));
    };

    for (const auto& e : entries) {
        offsets.append(quint32(out.size()));
        const quint32 crc = crc32(crc32(0, nullptr, 0),
                                  reinterpret_cast<const Bytef*>(e.data.constData()),
                                  uInt(e.data.size()));
        crcs.append(crc);
        QByteArray stored = e.data;
        quint16 method = 0;
        quint32 csize = quint32(stored.size());
        if (e.deflate && !e.data.isEmpty()) {
            method = 8;
            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            deflateInit2(&zs, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
            zs.next_in = reinterpret_cast<Bytef*>(stored.data());
            zs.avail_in = uInt(stored.size());
            QByteArray comp(stored.size() + 64, 0);
            zs.next_out = reinterpret_cast<Bytef*>(comp.data());
            zs.avail_out = uInt(comp.size());
            deflate(&zs, Z_FINISH);
            comp.resize(int(comp.size() - zs.avail_out));
            deflateEnd(&zs);
            stored = comp;
            csize = quint32(stored.size());
        }
        methods.append(method);
        m_sizes.append(csize);

        w32(0x04034b50); // local header
        w16(20);         // version needed
        w16(0);          // flags
        w16(method);
        w16(0);          // time
        w16(0);          // date
        w32(crc);
        w32(csize);
        w32(quint32(e.data.size()));
        w16(quint16(e.name.size()));
        w16(0);          // extra len
        out.append(e.name);
        out.append(stored);
    }

    const quint32 cdOffset = quint32(out.size());
    for (int i = 0; i < entries.size(); ++i) {
        w32(0x02014b50); // central dir
        w16(20); w16(20);
        w16(0);          // flags
        w16(methods[i]);
        w16(0);          // time
        w16(0);          // date
        w32(crcs[i]);
        w32(m_sizes[i]);
        w32(quint32(entries[i].data.size()));
        w16(quint16(entries[i].name.size()));
        w16(0); w16(0); w16(0); w16(0);
        w32(0);
        w32(offsets[i]);
        out.append(entries[i].name);
    }
    const quint32 cdSize = quint32(out.size()) - cdOffset;

    w32(0x06054b50); // EOCD
    w16(0); w16(0);
    w16(quint16(entries.size()));
    w16(quint16(entries.size()));
    w32(cdSize);
    w32(cdOffset);
    w16(0);
    return out;
}

// Minimal ustar with 2 files + one directory
QByteArray makeTestTar()
{
    QByteArray out;
    auto addHeader = [&out](const QByteArray& name, char type, quint64 size, const char* prefix = nullptr) {
        QByteArray h(512, 0);
        QByteArray n = name;
        if (prefix)
            n = QByteArray(prefix) + "/" + name;
        memcpy(h.data(), n.constData(), qMin<size_t>(99, n.size()));
        memcpy(h.data() + 100, "0000644", 7);   // mode
        memcpy(h.data() + 108, "0001750", 7);   // uid
        memcpy(h.data() + 116, "0001750", 7);   // gid
        QByteArray sz = QByteArray::number(qlonglong(size), 8).rightJustified(11, '0') + " ";
        memcpy(h.data() + 124, sz.constData(), 12);
        memcpy(h.data() + 136, "00000000000 ", 12); // mtime
        h[156] = type;
        memcpy(h.data() + 257, "ustar", 5);
        memcpy(h.data() + 263, "00", 2);
        quint32 sum = 0;
        for (int i = 0; i < 512; ++i)
            sum += i < 148 || i >= 156 ? quint8(h[i]) : ' ';
        QByteArray chk = QByteArray::number(qlonglong(sum), 8).rightJustified(6, '0') + "\0 ";
        memcpy(h.data() + 148, chk.constData(), 8);
        out.append(h);
    };
    auto addData = [&out](const QByteArray& data) {
        out.append(data);
        const int pad = int((512 - data.size() % 512) % 512);
        out.append(QByteArray(pad, 0));
    };

    addHeader("README.md", '0', 26);
    addData(QByteArrayLiteral("# Eclipse test tar\nsize=26\n"));
    addHeader("src", '5', 0);
    addHeader("app.py", '0', 12, "src");
    addData(QByteArrayLiteral("print('hi')\n"));

    out.append(QByteArray(1024, 0)); // two zero blocks
    return out;
}

QByteArray gzipCompress(const QByteArray& raw)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    deflateInit2(&zs, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY); // gzip
    QByteArray out(raw.size() + 64, 0);
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.constData()));
    zs.avail_in = uInt(raw.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = uInt(out.size());
    deflate(&zs, Z_FINISH);
    out.resize(int(out.size() - zs.avail_out));
    deflateEnd(&zs);
    return out;
}

} // namespace

class ArchiveTest : public QObject
{
    Q_OBJECT

private slots:
    void zipListAndExtract()
    {
        QTemporaryDir dir;
        const QString zipPath = dir.path() + QStringLiteral("/test.zip");
        {
            QFile f(zipPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(makeTestZip());
        }

        LocalFileArchiveSource source(zipPath);
        QVERIFY(source.isOpen());

        ZipReader reader;
        QString err;
        QVERIFY2(reader.open(&source, &err), qPrintable(err));
        QCOMPARE(reader.entries().size(), 3);
        QVERIFY(reader.entries().at(0).path == QLatin1String("hello.txt")
                || reader.entries().at(0).path == QLatin1String("docs")
                || reader.entries().at(0).path == QLatin1String("src/main.cpp"));

        const QString dest = dir.path() + QStringLiteral("/out/hello.txt");
        QVERIFY2(reader.extractEntry(&source, QStringLiteral("hello.txt"), dest, &err),
                 qPrintable(err));
        QFile out(dest);
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), QByteArrayLiteral("Hello from Eclipse SSH Desktop!"));
    }

    void tarListAndExtract()
    {
        QTemporaryDir dir;
        const QString tarPath = dir.path() + QStringLiteral("/test.tar");
        {
            QFile f(tarPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(makeTestTar());
        }

        LocalFileArchiveSource source(tarPath);
        TarReader reader(false);
        QString err;
        QVERIFY2(reader.open(&source, &err), qPrintable(err));

        bool sawReadme = false, sawApp = false, sawSrc = false;
        for (const auto& e : reader.entries()) {
            if (e.path == QLatin1String("README.md"))
                sawReadme = e.size == 26;
            if (e.path == QLatin1String("src/app.py"))
                sawApp = e.size == 12;
            if (e.path == QLatin1String("src"))
                sawSrc = e.isDir;
        }
        QVERIFY(sawReadme);
        QVERIFY(sawApp);
        QVERIFY(sawSrc);

        const QString dest = dir.path() + QStringLiteral("/out/app.py");
        QVERIFY2(reader.extractEntry(QStringLiteral("src/app.py"), dest, &err), qPrintable(err));
        QFile out(dest);
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), QByteArrayLiteral("print('hi')\n"));
    }

    void tarGzStreamedScan()
    {
        QTemporaryDir dir;
        const QString tgz = dir.path() + QStringLiteral("/test.tar.gz");
        {
            QFile f(tgz);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(gzipCompress(makeTestTar()));
        }

        LocalFileArchiveSource source(tgz);
        TarReader reader(true);
        QString err;
        QVERIFY2(reader.open(&source, &err), qPrintable(err));
        QCOMPARE(reader.entries().size(), 3);
    }

    void detectsFormats()
    {
        QCOMPARE(int(ArchiveService::detect(QStringLiteral("backup.tar.gz"))), int(ArchiveService::Kind::TarGz));
        QCOMPARE(int(ArchiveService::detect(QStringLiteral("x.ZIP"))), int(ArchiveService::Kind::Zip));
        QCOMPARE(int(ArchiveService::detect(QStringLiteral("a.tgz"))), int(ArchiveService::Kind::TarGz));
        QCOMPARE(int(ArchiveService::detect(QStringLiteral("b.7z"))), int(ArchiveService::Kind::SevenZip));
    }
};

QTEST_GUILESS_MAIN(ArchiveTest)
#include "ArchiveTest.moc"
