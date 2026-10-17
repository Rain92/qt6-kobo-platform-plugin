#ifndef QKOBOFBSCREEN_H
#define QKOBOFBSCREEN_H

#include <QtFbSupport/private/qfbscreen_p.h>

#include "dither.h"
#include "einkenums.h"
#include "fbink.h"
#include "kobodevicedescriptor.h"

class QPainter;

class KoboFbScreen : public QFbScreen
{
    Q_OBJECT
public:
    KoboFbScreen(const QStringList &args, KoboDeviceDescriptor *koboDevice);
    ~KoboFbScreen();

    bool initialize() override;

    QDpi logicalDpi() const override;
    QDpi logicalBaseDpi() const override;

    void setFullScreenRefreshMode(WaveForm waveform);

    void clearScreen(bool waitForCompleted);

    void enableDithering(bool softwareDithering, bool hardwareDithering);

    void doManualRefresh(const QRect &region);

    bool setScreenRotation(ScreenRotation r, int bpp = 8);

    ScreenRotation getScreenRotation();

    FBInkConfig *getFBInkConfig();
    FBInkState *getFBInkState();

    QRegion doRedraw() override;

private:
    void ditherRegionToFramebuffer(const QRect &region);
    bool canSoftwareDither() const;
    bool drawFromWindow(const QRect &region, bool dither);

    KoboDeviceDescriptor *koboDevice;

    QStringList mArgs;
    int mFbFd;
    bool debug;
    bool profile;

    QImage mFbScreenImage;
    int mBytesPerLine;

    QPainter *mBlitter;

    FBInkState fbink_state;

    struct
    {
        unsigned char *bufferPtr;
        size_t bufferSize;
    } memmapInfo;

    FBInkConfig fbink_cfg;

    bool waitForRefresh;
    bool useHardwareDithering;
    bool useSoftwareDithering;

    WFM_MODE_INDEX_T waveFormFullscreen;
    WFM_MODE_INDEX_T waveFormPartial;
    WFM_MODE_INDEX_T waveFormFast;

    int requestedBpp;
    int logicalDpiTarget;

    uint32_t originalRotation;
    uint32_t originalBpp;
};

#endif  // QKOBOFBSCREEN_H
