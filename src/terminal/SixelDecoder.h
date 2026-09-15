#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>

#include <functional>
#include <vector>

namespace eclipse {

// ---------------------------------------------------------------------------
// SixelDecoder - self-contained DEC SIXEL graphics decoder (no dependencies
// beyond QtGui). It is deliberately free of QObject/moc so it can be unit
// tested and reused from any thread.
//
// A sixel sequence is a DCS string:  ESC P P1;P2;P3 q <payload> ST
// The decoder consumes the *payload* only (bytes between the final 'q' of the
// introducer and the ST terminator). The introducer and terminator are NOT
// part of the payload.
//
// Implemented subset (standard DEC sixel, DEC STD 070):
//   - '!'  DECGRI  repeat next glyph N times
//   - '$'  DECGCR  carriage return inside the band (x = 0)
//   - '-'  DECGNL  newline: next band of 6 pixel rows (y += 6, x = 0)
//   - '#'  DECGCI  color select, or color definition "#Pc;Pu;Px;Py;Pz"
//                  (Pu=1: HSL with 0..360 hue and 0..100 %/lum, Pu=2: RGB,
//                  0..100 % per DEC spec, 0..255 tolerated)
//   - '"'  raster attributes: "P1;P2;P3;P4 (canvas size preallocation)
//   - CR/LF tolerated like '$'/'-'
//   - sixel charset glyphs 0x3F..0x7E: each encodes 6 vertical pixels
//     (bit 0 = top row of the band), bands of 6 pixel rows
//
// Usage (streaming):
//   SixelDecoder dec([](const QImage& img) { consume(img); });
//   dec.begin(transparentBackground);
//   dec.feed(chunk1);
//   dec.feed(chunk2);
//   dec.finish();          // callback fires here if any pixel was set
// Usage (one-shot):
//   QImage img = SixelDecoder::decode(payload, transparentBackground);
//
// Output: Format_RGB32 image sized to the written content (or Format_ARGB32
// with transparent background when the introducer requested P2 = 1).
// Canvas is capped at 4096 x 4096 pixels to bound memory.
// ---------------------------------------------------------------------------
class SixelDecoder
{
public:
    using ImageCallback = std::function<void(const QImage&)>;

    explicit SixelDecoder(ImageCallback onImage = {});

    // (Re)start a decode session. transparentBackground = DCS introducer P2 == 1
    // ("do not set background pixels"), which yields an ARGB32 result.
    void begin(bool transparentBackground);

    // Feed raw payload bytes (may be called repeatedly, in any chunking).
    void feed(const QByteArray& data);

    // End of payload: finalizes the image and fires the callback (if the
    // session produced any pixels).
    void finish();

    bool active() const { return m_started; }

    // One-shot convenience: decode a complete payload in one call.
    static QImage decode(const QByteArray& payload, bool transparentBackground = false);

    // DCS introducer parameter bytes (between "ESC P" and the final 'q', e.g.
    // "0;1;0" or ";1") -> true when P2 == 1 requests a transparent background.
    static bool introRequestsTransparency(const QByteArray& dcsIntroParams);

private:
    void reset(bool transparentBackground);
    void handleByte(unsigned char b);
    void command(unsigned char b, const std::vector<int>& params);
    void selectOrDefineColor(const std::vector<int>& params);
    void drawGlyph(unsigned char value, int repeat);
    void growCanvas(int minW, int minH);
    void finishImage();

    ImageCallback m_onImage;
    bool m_started = false;
    bool m_transparentBg = false;

    QByteArray m_paramText;   // digits/';' collected before a command char
    int m_repeat = 1;         // pending '!' repeat count
    int m_color = 0;          // current palette index
    int m_x = 0;              // pen column (pixels)
    int m_y = 0;              // pen row (top of current 6-pixel band)
    int m_maxX = 0;           // extent of written pixels (exclusive)
    int m_maxY = 0;
    QImage m_canvas;          // ARGB32 working image
    QColor m_palette[256];
};

} // namespace eclipse
