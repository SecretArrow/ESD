#include "SixelDecoder.h"

#include <QPainter>

namespace eclipse {

namespace {

constexpr int kBandHeight = 6;
constexpr int kMaxSixelWidth = 4096;
constexpr int kMaxSixelHeight = 4096;

// DEC default palette: color numbers follow the DEC hue codes
// (0=black 1=blue 2=red 3=green 4=magenta 5=cyan 6=yellow 7=white).
QColor defaultPaletteColor(int index)
{
    switch (index) {
    case 0:  return QColor(0x00, 0x00, 0x00);
    case 1:  return QColor(0x00, 0x00, 0xb0);
    case 2:  return QColor(0xb0, 0x00, 0x00);
    case 3:  return QColor(0x00, 0xb0, 0x00);
    case 4:  return QColor(0xb0, 0x00, 0xb0);
    case 5:  return QColor(0x00, 0xb0, 0xb0);
    case 6:  return QColor(0xb0, 0xb0, 0x00);
    case 7:  return QColor(0xb0, 0xb0, 0xb0);
    default: {
        const int v = 60 + (index % 12) * 15;
        return QColor(v, v, v);
    }
    }
}

} // namespace

SixelDecoder::SixelDecoder(ImageCallback onImage)
    : m_onImage(std::move(onImage))
{
}

void SixelDecoder::reset(bool transparentBackground)
{
    m_transparentBg = transparentBackground;
    m_paramText.clear();
    m_repeat = 1;
    m_color = 0;
    m_x = 0;
    m_y = 0;
    m_maxX = 0;
    m_maxY = 0;
    for (int i = 0; i < 256; ++i)
        m_palette[i] = defaultPaletteColor(i);
    m_canvas = QImage(256, 64, QImage::Format_ARGB32);
    m_canvas.fill(Qt::transparent);
    m_started = true;
}

void SixelDecoder::begin(bool transparentBackground)
{
    reset(transparentBackground);
}

void SixelDecoder::feed(const QByteArray& data)
{
    if (!m_started || data.isEmpty())
        return;
    for (int i = 0; i < data.size(); ++i)
        handleByte(static_cast<unsigned char>(data.at(i)));
}

void SixelDecoder::finish()
{
    if (!m_started)
        return;
    finishImage();
    m_started = false;
}

void SixelDecoder::handleByte(unsigned char b)
{
    // Numeric parameters accumulate until the next command byte.
    if ((b >= '0' && b <= '9') || b == ';') {
        m_paramText.append(char(b));
        return;
    }
    if (b == 0x00) // NUL padding
        return;

    std::vector<int> params;
    if (!m_paramText.isEmpty()) {
        const QList<QByteArray> parts = m_paramText.split(';');
        params.reserve(size_t(parts.size()));
        for (const QByteArray& p : parts) {
            bool ok = false;
            const int v = p.toInt(&ok);
            params.push_back(ok ? v : 0);
        }
        m_paramText.clear();
    }
    command(b, params);
}

void SixelDecoder::command(unsigned char b, const std::vector<int>& params)
{
    // Every command except '!' cancels a pending repeat count.
    m_repeat = 1;

    switch (b) {
    case '#':
        selectOrDefineColor(params);
        break;
    case '!': // DECGRI repeat
        m_repeat = params.empty() ? 1 : qBound(1, params.front(), 65535);
        break;
    case '$': // DECGCR carriage return, same band
    case 0x0d:
        m_x = 0;
        break;
    case '-': // DECGNL next band of 6 pixel rows
    case 0x0a: // LF: treat like a band advance (keep x)
        if (b == '-')
            m_x = 0;
        m_y += kBandHeight;
        break;
    case '"': // raster attributes "P1;P2;P3;P4 -> preallocate canvas
        if (params.size() >= 4) {
            const int w = qBound(1, params[2], kMaxSixelWidth);
            const int h = qBound(1, params[3], kMaxSixelHeight);
            growCanvas(w, h);
        }
        break;
    default:
        if (b >= 0x3f && b <= 0x7e)
            drawGlyph(b - 0x3f, m_repeat);
        // Anything else: ignored (unknown/unsupported sixel command).
        break;
    }
}

void SixelDecoder::selectOrDefineColor(const std::vector<int>& params)
{
    if (params.empty()) {
        m_color = 0;
        return;
    }
    const int index = qBound(0, params.front(), 255);
    if (params.size() >= 5) {
        const int mode = params[1];
        if (mode == 1) {
            // HSL: hue 0..360, saturation/luminosity given as 0..100 %
            m_palette[index] = QColor::fromHsl(qBound(0, params[2], 359),
                                               qBound(0, params[3], 100) * 255 / 100,
                                               qBound(0, params[4], 100) * 255 / 100);
        } else {
            // RGB: DEC sends 0..100 per cent; tolerate raw 0..255 values.
            const auto chan = [](int v) {
                return qBound(0, v <= 100 ? v * 255 / 100 : v, 255);
            };
            m_palette[index] = QColor(chan(params[2]), chan(params[3]), chan(params[4]));
        }
    }
    m_color = index;
}

void SixelDecoder::drawGlyph(unsigned char value, int repeat)
{
    if (repeat < 1)
        repeat = 1;
    const QColor color = m_palette[m_color & 0xff];

    for (int rep = 0; rep < repeat; ++rep) {
        const int px = m_x + rep;
        if (px >= kMaxSixelWidth)
            break;
        if (px >= m_canvas.width() || m_y + kBandHeight > m_canvas.height())
            growCanvas(px + 1, m_y + kBandHeight);
        for (int bit = 0; bit < kBandHeight; ++bit) {
            if (!(value & (1u << bit)))
                continue;
            const int py = m_y + bit;
            if (px < m_canvas.width() && py < m_canvas.height()) {
                m_canvas.setPixel(px, py, color.rgba());
                if (px + 1 > m_maxX)
                    m_maxX = px + 1;
                if (py + 1 > m_maxY)
                    m_maxY = py + 1;
            }
        }
    }
    m_x += repeat;
}

void SixelDecoder::growCanvas(int minW, int minH)
{
    const int newW = qMin(kMaxSixelWidth, qMax(qMax(minW, m_canvas.width()), 256));
    const int newH = qMin(kMaxSixelHeight, qMax(qMax(minH, m_canvas.height()), 64));
    if (newW == m_canvas.width() && newH == m_canvas.height())
        return;
    QImage next(newW, newH, QImage::Format_ARGB32);
    next.fill(Qt::transparent);
    if (!m_canvas.isNull()) {
        QPainter p(&next);
        p.drawImage(0, 0, m_canvas);
    }
    m_canvas = next;
}

void SixelDecoder::finishImage()
{
    if (m_maxX <= 0 || m_maxY <= 0) {
        m_canvas = QImage();
        return;
    }
    const int w = qMin(m_maxX, m_canvas.width());
    const int h = qMin(m_maxY, m_canvas.height());
    const QImage content = m_canvas.copy(0, 0, w, h);

    QImage out;
    if (m_transparentBg) {
        out = content.convertToFormat(QImage::Format_ARGB32);
    } else {
        // Composite the (alpha-masked) content over color 0, per DEC P2 != 1.
        out = QImage(w, h, QImage::Format_RGB32);
        out.fill(m_palette[0]);
        QPainter p(&out);
        p.drawImage(0, 0, content);
    }

    if (m_onImage && !out.isNull())
        m_onImage(out);
    m_canvas = QImage();
}

QImage SixelDecoder::decode(const QByteArray& payload, bool transparentBackground)
{
    QImage result;
    SixelDecoder dec([&result](const QImage& img) { result = img; });
    dec.begin(transparentBackground);
    dec.feed(payload);
    dec.finish();
    return result;
}

bool SixelDecoder::introRequestsTransparency(const QByteArray& dcsIntroParams)
{
    // P1;P2;P3: P2 == 1 means "leave unset pixels transparent".
    const QList<QByteArray> parts = dcsIntroParams.split(';');
    if (parts.size() < 2)
        return false;
    bool ok = false;
    const int p2 = parts.at(1).toInt(&ok);
    return ok && p2 == 1;
}

} // namespace eclipse
