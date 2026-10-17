#include "kobofbscreen.h"

#include <linux/fb.h>
#include <sys/ioctl.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QRegularExpression>
#include <QtFbSupport/private/qfbbackingstore_p.h>
#include <QtFbSupport/private/qfbwindow_p.h>
#include <QtGui/QPainter>
#include <cstring>

// fb_var_screeninfo.grayscale value for 8bpp grayscale, the same in the mxcfb and MTK hwtcon headers
#ifndef GRAYSCALE_8BIT
#define GRAYSCALE_8BIT 0x1
#endif

#define SMALLTHRESHOLD1 60
#define SMALLTHRESHOLD2 40
#define FULLSCREENTOLERANCE 80

static QImage::Format determineFormat(int fbfd, int depth)
{
    fb_var_screeninfo info;
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &info))
        return QImage::Format_Invalid;

    const fb_bitfield rgba[4] = {info.red, info.green, info.blue, info.transp};

    QImage::Format format = QImage::Format_Invalid;

    switch (depth)
    {
        case 32:
        {
            const fb_bitfield argb8888[4] = {{16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {24, 8, 0}};
            const fb_bitfield abgr8888[4] = {{0, 8, 0}, {8, 8, 0}, {16, 8, 0}, {24, 8, 0}};
            if (memcmp(rgba, argb8888, 4 * sizeof(fb_bitfield)) == 0)
                format = QImage::Format_ARGB32;
            else if (memcmp(rgba, argb8888, 3 * sizeof(fb_bitfield)) == 0)
                format = QImage::Format_RGB32;
            // Byte order R, G, B, A, which is what the MTK devices use at 32bpp
            else if (memcmp(rgba, abgr8888, 4 * sizeof(fb_bitfield)) == 0)
                format = QImage::Format_RGBA8888;
            else if (memcmp(rgba, abgr8888, 3 * sizeof(fb_bitfield)) == 0)
                format = QImage::Format_RGBX8888;
            break;
        }
        case 24:
        {
            const fb_bitfield rgb888[4] = {{16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {0, 0, 0}};
            const fb_bitfield bgr888[4] = {{0, 8, 0}, {8, 8, 0}, {16, 8, 0}, {0, 0, 0}};
            if (memcmp(rgba, rgb888, 3 * sizeof(fb_bitfield)) == 0)
                format = QImage::Format_RGB888;
            else if (memcmp(rgba, bgr888, 3 * sizeof(fb_bitfield)) == 0)
                format = QImage::Format_BGR888;
            break;
        }
        case 16:
            format = QImage::Format_RGB16;
            break;
        case 8:
            format = QImage::Format_Grayscale8;
            break;
        default:
            break;
    }

    return format;
}

// The values of the public WaveForm enum don't all match FBInk's (e.g. WaveForm_AUTO), so map them explicitly.
static WFM_MODE_INDEX_T toFBInkWaveForm(WaveForm waveform)
{
    switch (waveform)
    {
        case WaveForm_AUTO:
            return WFM_AUTO;
        case WaveForm_DU:
            return WFM_DU;
        case WaveForm_GC16:
            return WFM_GC16;
        case WaveForm_GC4:
            return WFM_GC4;
        case WaveForm_A2:
            return WFM_A2;
        case WaveForm_GL16:
            return WFM_GL16;
        case WaveForm_REAGL:
            return WFM_REAGL;
        case WaveForm_REAGLD:
            return WFM_REAGLD;
    }
    return WFM_AUTO;
}

KoboFbScreen::KoboFbScreen(const QStringList &args, KoboDeviceDescriptor *koboDevice)
    : koboDevice(koboDevice),
      mArgs(args),
      mFbFd(-1),
      debug(false),
      profile(false),
      mFbScreenImage(),
      mBytesPerLine(0),
      mBlitter(nullptr),
      fbink_state({0}),
      memmapInfo({0}),
      fbink_cfg({0}),
      waitForRefresh(false),
      useHardwareDithering(false),
      useSoftwareDithering(true),
      requestedBpp(8),
      logicalDpiTarget(0),
      originalRotation(0),
      originalBpp(0)
{
    waveFormFullscreen = WFM_GC16;
    waveFormPartial = WFM_AUTO;
    waveFormFast = WFM_A2;

    if (koboDevice->isREAGL)
    {
        this->waveFormPartial = WFM_REAGLD;
        this->waveFormFast = WFM_DU;
    }
    else if (koboDevice->mark >= 7 || koboDevice->isSunxi || koboDevice->isMTK)
    {
        this->waveFormPartial = WFM_REAGL;
        this->waveFormFast = WFM_DU;
    }
}

KoboFbScreen::~KoboFbScreen()
{
    delete mBlitter;

    if (mFbFd != -1)
    {
        uint8_t grayscale = originalBpp == 8 ? GRAYSCALE_8BIT : 0;
        if (fbink_set_fb_info(mFbFd, originalRotation, originalBpp, grayscale, &fbink_cfg) < EXIT_SUCCESS)
            qDebug() << "Failed to restore the original rotation and bpp.";

        fbink_close(mFbFd);
    }
}

bool KoboFbScreen::initialize()
{
    QRegularExpression sizeRx("size=(\\d+)x(\\d+)");
    QRegularExpression dpiRx("logicaldpitarget=(\\d+)");
    QRegularExpression bppRx("bpp=(\\d+)");

    QRect userGeometry;

    // Parse arguments
    for (const QString &arg : std::as_const(mArgs))
    {
        QRegularExpressionMatch match;
        if (arg.contains(sizeRx, &match))
            userGeometry.setSize(QSize(match.captured(1).toInt(), match.captured(2).toInt()));
        else if (arg.contains(dpiRx, &match))
            logicalDpiTarget = match.captured(1).toInt();
        else if (arg.contains(bppRx, &match))
            requestedBpp = match.captured(1).toInt();
        else if (arg.startsWith("debug"))
            debug = true;
        else if (arg == QLatin1String("profile"))
            profile = true;
    }

    fbink_cfg.is_verbose = debug;
    fbink_cfg.is_quiet = !debug;

    // Open framebuffer and keep it around, then setup globals.
    if ((mFbFd = fbink_open()) < EXIT_SUCCESS)
    {
        qWarning() << "kobofb: Failed to open the framebuffer";
        return false;
    }

    if (fbink_init(mFbFd, &fbink_cfg) < EXIT_SUCCESS)
    {
        qWarning() << "kobofb: Failed to initialize FBInk.";
        return false;
    }

    fbink_get_state(&fbink_cfg, &fbink_state);
    originalBpp = fbink_state.bpp;
    originalRotation = fbink_state.current_rota;

    if (!setScreenRotation(RotationUR, requestedBpp))
        return false;

    QFbScreen::initializeCompositor();

    if (mFbScreenImage.isNull())
        qWarning() << "kobofb: Error, mFbScreenImage is invalid!";

    if (logicalDpiTarget > 0)
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // Qt 6 derives the same factor from logicalDpi() / logicalBaseDpi()
        qputenv("QT_SCREEN_SCALE_FACTORS",
                QString::number(koboDevice->dpi / (double)logicalDpiTarget, 'g', 8).toLatin1());
#endif
    }

    return true;
}

// Qt 5 used the physical dpi as logical dpi by default, so font point sizes matched the real world size.
// Qt 6 defaults to 96, which would make every pt sized font ~3 times smaller on a 300 dpi Kobo.
QDpi KoboFbScreen::logicalDpi() const
{
    return QDpi(koboDevice->dpi, koboDevice->dpi);
}

// Qt 6 scales the UI by logicalDpi() / logicalBaseDpi(), so this is 1 unless a logicaldpitarget is set.
QDpi KoboFbScreen::logicalBaseDpi() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (logicalDpiTarget > 0)
        return QDpi(logicalDpiTarget, logicalDpiTarget);
    return logicalDpi();
#else
    return QFbScreen::logicalBaseDpi();
#endif
}

bool KoboFbScreen::setScreenRotation(ScreenRotation r, int bpp)
{
    uint32_t rota_native = fbink_rota_canonical_to_native(r);
    uint8_t grayscale = bpp == 8 ? GRAYSCALE_8BIT : 0;

    int rv = 0;
    if ((rv = fbink_set_fb_info(mFbFd, rota_native, bpp, grayscale, &fbink_cfg)) < EXIT_SUCCESS)
        qWarning() << "kobofb: Failed to set rotation and bpp:" << rv;

    fbink_get_state(&fbink_cfg, &fbink_state);

    // Some drivers refuse certain bitdepths (e.g. the Elipsa 2E is stuck at 32bpp)
    if (fbink_state.bpp != (uint32_t)bpp)
        qWarning() << "kobofb: Requested" << bpp << "bpp, but the framebuffer is at" << fbink_state.bpp << "bpp";

    mBytesPerLine = fbink_state.scanline_stride;
    koboDevice->width = fbink_state.screen_width;
    koboDevice->height = fbink_state.screen_height;

    if (debug)
        qDebug() << "Screen info:" << fbink_state.screen_width << fbink_state.screen_height
                 << "rotation:" << fbink_state.current_rota
                 << "rotation canonical:" << fbink_rota_native_to_canonical(fbink_state.current_rota)
                 << "bpp:" << fbink_state.bpp;

    mGeometry = {0, 0, koboDevice->width, koboDevice->height};

    koboDevice->physicalWidth = qRound(koboDevice->width * 25.4 / koboDevice->dpi);
    koboDevice->physicalHeight = qRound(koboDevice->height * 25.4 / koboDevice->dpi);
    mPhysicalSize = QSizeF(koboDevice->physicalWidth, koboDevice->physicalHeight);

    if ((memmapInfo.bufferPtr = fbink_get_fb_pointer(mFbFd, &memmapInfo.bufferSize)) == NULL)
    {
        qWarning() << "kobofb: Failed to get fb data or memmap screen";
        return false;
    }

    if (debug)
        qDebug() << "Allocated screen buffer. Stride:" << fbink_state.scanline_stride
                 << "buffer size:" << memmapInfo.bufferSize;

    mDepth = fbink_state.bpp;
    mFormat = determineFormat(mFbFd, mDepth);

    if (mFormat == QImage::Format_Invalid)
    {
        qWarning() << "kobofb: Unsupported framebuffer format at" << mDepth << "bpp";
        return false;
    }

    mFbScreenImage =
        QImage(memmapInfo.bufferPtr, mGeometry.width(), mGeometry.height(), mBytesPerLine, mFormat);

    return true;
}

ScreenRotation KoboFbScreen::getScreenRotation()
{
    return (ScreenRotation)fbink_rota_native_to_canonical(fbink_state.current_rota);
}

FBInkConfig *KoboFbScreen::getFBInkConfig()
{
    return &fbink_cfg;
}

FBInkState *KoboFbScreen::getFBInkState()
{
    return &fbink_state;
}

void KoboFbScreen::setFullScreenRefreshMode(WaveForm waveform)
{
    this->waveFormFullscreen = toFBInkWaveForm(waveform);
}

void KoboFbScreen::clearScreen(bool waitForCompleted)
{
    FBInkRect r = {0, 0, static_cast<unsigned short>(mGeometry.width()),
                   static_cast<unsigned short>(mGeometry.height())};
    fbink_cls(mFbFd, &fbink_cfg, &r, true);

    if (waitForCompleted && !fbink_state.unreliable_wait_for)
        fbink_wait_for_complete(mFbFd, LAST_MARKER);
}

void KoboFbScreen::enableDithering(bool softwareDithering, bool hardwareDithering)
{
    useHardwareDithering = hardwareDithering;
    useSoftwareDithering = softwareDithering;
}

// The dither routines work on 8bit grayscale buffers only
bool KoboFbScreen::canSoftwareDither() const
{
    return useSoftwareDithering && mScreenImage.format() == QImage::Format_Grayscale8 &&
           mFbScreenImage.format() == QImage::Format_Grayscale8;
}

// Dithers a region of the composited screen image straight into the framebuffer, which saves the
// intermediate dithered copy and the extra pass to blit it.
void KoboFbScreen::ditherRegionToFramebuffer(const QRect &region)
{
    const int srcStride = mScreenImage.bytesPerLine();
    const uchar *src = mScreenImage.constBits() + region.top() * srcStride + region.left();
    uchar *dst = memmapInfo.bufferPtr + region.top() * mBytesPerLine + region.left();

    ditherBufferRegion(dst, mBytesPerLine, src, srcStride, region.left(), region.top(), region.width(),
                       region.height());
}

// The windows' backing stores have the screen's format, so a region that lies in one window, with no other window
// over it, is the same in the screen image as in that window's backing store. It is drawn straight from there into
// the framebuffer: QFbScreen::doRedraw() would fill the region and copy the window into the screen image first
// (~6 ms for the whole screen on the Aura One, e.g. at every page turn). Qt redraws every region it composites
// from the backing stores, and the screen image is only read right after that, so the regions left out of it are
// never used stale.
bool KoboFbScreen::drawFromWindow(const QRect &region, bool dither)
{
    // the stack is topmost first, the topmost visible window that the region overlaps is the one that shows
    for (QFbWindow *window : std::as_const(mWindowStack))
    {
        if (!window->window()->isVisible())
            continue;

        const QRect geometry = window->geometry().translated(-mGeometry.topLeft());
        if (!geometry.intersects(region))
            continue;

        QFbBackingStore *backingStore = window->backingStore();
        if (!geometry.contains(region) || !backingStore)
            return false;

        backingStore->lock();
        const QImage image = backingStore->image();
        const QRect source = region.translated(-geometry.topLeft());
        const bool drawn = image.format() == mFbScreenImage.format() && image.rect().contains(source);
        if (drawn)
        {
            const int srcStride = image.bytesPerLine();
            const uchar *src = image.constBits() + source.top() * srcStride + source.left();
            uchar *dst = memmapInfo.bufferPtr + region.top() * mBytesPerLine + region.left();
            if (dither)
                ditherBufferRegion(dst, mBytesPerLine, src, srcStride, region.left(), region.top(), region.width(),
                                   region.height());
            else
                for (int y = 0; y < region.height(); y++)
                    memcpy(dst + y * mBytesPerLine, src + y * srcStride, region.width());
        }
        backingStore->unlock();
        return drawn;
    }

    // no window, Qt fills the region
    return false;
}

void KoboFbScreen::doManualRefresh(const QRect &region)
{
    bool isFullRefresh = region.width() >= mGeometry.width() - FULLSCREENTOLERANCE &&
                         region.height() >= mGeometry.height() - FULLSCREENTOLERANCE;

    bool isSmall = (region.width() < SMALLTHRESHOLD1 && region.height() < SMALLTHRESHOLD1) ||
                   (region.width() + region.height() < SMALLTHRESHOLD2);

    if (isFullRefresh)
        fbink_cfg.wfm_mode = this->waveFormFullscreen;
    else if (isSmall)
        fbink_cfg.wfm_mode = this->waveFormFast;
    else
        fbink_cfg.wfm_mode = this->waveFormPartial;

    fbink_cfg.is_flashing = isFullRefresh;
    // Ordered is the only mode supported by the EPDC v2 hardware dithering
    fbink_cfg.dithering_mode = useHardwareDithering ? HWD_ORDERED : HWD_PASSTHROUGH;

    fbink_refresh(mFbFd, region.top(), region.left(), region.width(), region.height(), &fbink_cfg);

    if (waitForRefresh && !fbink_state.unreliable_wait_for)
        fbink_wait_for_complete(mFbFd, LAST_MARKER);
}

QRegion KoboFbScreen::doRedraw()
{
    QElapsedTimer timer;
    if (profile)
        timer.start();

    const QRect screenRect(QPoint(0, 0), mGeometry.size());
    QRect r;

    // the regions that lie in one window are drawn from it (see drawFromWindow()), with the 8 bit framebuffer that
    // both formats are
    QRegion direct;
    if (!mCursor && mFbScreenImage.format() == QImage::Format_Grayscale8)
    {
        const bool dither = canSoftwareDither();
        for (const QRect &dirty : std::as_const(mRepaintRegion))
        {
            const QRect rect = dirty.intersected(screenRect);
            if (!rect.isEmpty() && drawFromWindow(rect, dither))
            {
                direct += rect;
                r = r.united(rect);
            }
        }
        mRepaintRegion -= direct;
    }

    const qint64 directNs = profile ? timer.nsecsElapsed() : 0;

    QRegion touched = QFbScreen::doRedraw();

    if (touched.isEmpty() && direct.isEmpty())
        return touched;

    const qint64 compositeNs = profile ? timer.nsecsElapsed() - directNs : 0;

    if (canSoftwareDither())
    {
        for (const QRect &touchedRect : touched)
        {
            const QRect rect = touchedRect.intersected(screenRect);
            if (rect.isEmpty())
                continue;
            ditherRegionToFramebuffer(rect);
            r = r.united(rect);
        }
    }
    else
    {
        if (!mBlitter)
            mBlitter = new QPainter(&mFbScreenImage);

        mBlitter->setCompositionMode(QPainter::CompositionMode_Source);
        for (const QRect &rect : touched)
        {
            mBlitter->drawImage(rect, mScreenImage, rect);
            r = r.united(rect);
        }
    }

    touched += direct;
    if (r.isEmpty())
        return touched;

    const qint64 blitNs = profile ? timer.nsecsElapsed() - directNs - compositeNs : 0;

    doManualRefresh(r);

    if (profile)
    {
        const qint64 refreshNs = timer.nsecsElapsed() - directNs - compositeNs - blitNs;
        qDebug().nospace() << "[perf] redraw " << r.width() << "x" << r.height() << " direct " << directNs / 1e6
                           << " ms, composite " << compositeNs / 1e6 << " ms, "
                           << (canSoftwareDither() ? "dither " : "blit ") << blitNs / 1e6 << " ms, refresh "
                           << refreshNs / 1e6 << " ms";
    }

    return touched;
}
