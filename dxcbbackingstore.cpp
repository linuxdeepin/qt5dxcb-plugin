#include "dxcbbackingstore.h"
#include "xcbwindowhook.h"
#include "vtablehook.h"
#include "utility.h"
#include "global.h"

#include "qxcbbackingstore.h"

#include <QDebug>
#include <QPainter>
#include <QEvent>
#include <QWidget>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainterPathStroker>
#include <QGuiApplication>
#include <QVariantAnimation>
#include <QTimer>

#include <private/qwidgetwindow_p.h>
#include <qpa/qplatformgraphicsbuffer.h>
#include <qpa/qplatformscreen.h>
#include <qpa/qplatformcursor.h>
#include <qpa/qplatformnativeinterface.h>

PUBLIC_CLASS(QMouseEvent, WindowEventListener);
PUBLIC_CLASS(QWheelEvent, WindowEventListener);
PUBLIC_CLASS(QResizeEvent, WindowEventListener);
PUBLIC_CLASS(QWidget, WindowEventListener);
PUBLIC_CLASS(QWindow, WindowEventListener);

PUBLIC_CLASS(QXcbWindow, DXcbBackingStore);

class WindowEventListener : public QObject
{
public:
    explicit WindowEventListener(DXcbBackingStore *store)
        : QObject(0)
        , m_store(store)
    {
        store->window()->installEventFilter(this);

        cursorAnimation.setDuration(50);
        cursorAnimation.setEasingCurve(QEasingCurve::InExpo);

        connect(&cursorAnimation, &QVariantAnimation::valueChanged,
                this, &WindowEventListener::onAnimationValueChanged);

        startAnimationTimer.setSingleShot(true);
        startAnimationTimer.setInterval(300);

        connect(&startAnimationTimer, &QTimer::timeout,
                this, &WindowEventListener::startAnimation);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) Q_DECL_OVERRIDE
    {
        QWindow *window = qobject_cast<QWindow*>(obj);

        if (!window)
            return false;

        const QRect &window_geometry = window->geometry();
//        qDebug() << obj << event->type() << window_geometry;

        switch ((int)event->type()) {
        case QEvent::Wheel: {
            DQWheelEvent *e = static_cast<DQWheelEvent*>(event);

            if (!window_geometry.contains(e->globalPos()))
                return true;

            e->p -= m_store->windowOffset();

            break;
        }
        case QEvent::MouseMove:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease: {
            DQMouseEvent *e = static_cast<DQMouseEvent*>(event);

            e->l -= m_store->windowOffset();
            e->w -= m_store->windowOffset();

            if (window->minimumSize() == window->maximumSize())
                break;

            const QRect &window_visible_rect = m_store->windowValidRect.translated(window_geometry.topLeft());

            if (!leftButtonPressed && (!window_visible_rect.contains(e->globalPos())
                    || !m_store->m_clipPath.contains(e->windowPos()))) {
                if (event->type() == QEvent::MouseMove) {
                    bool isFixedWidth = window->minimumWidth() == window->maximumWidth();
                    bool isFixedHeight = window->minimumHeight() == window->maximumHeight();

                    Utility::CornerEdge mouseCorner;
                    QRect cornerRect;
                    const QRect window_real_geometry = window_visible_rect
                            + QMargins(MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS);

                    if (isFixedWidth || isFixedHeight)
                        goto set_edge;

                    /// begin set cursor corner type
                    cornerRect.setSize(QSize(MOUSE_MARGINS * 2, MOUSE_MARGINS * 2));
                    cornerRect.moveTopLeft(window_real_geometry.topLeft());

                    if (cornerRect.contains(e->globalPos())) {
                        mouseCorner = Utility::TopLeftCorner;

                        goto set_cursor;
                    }

                    cornerRect.moveTopRight(window_real_geometry.topRight());

                    if (cornerRect.contains(e->globalPos())) {
                        mouseCorner = Utility::TopRightCorner;

                        goto set_cursor;
                    }

                    cornerRect.moveBottomRight(window_real_geometry.bottomRight());

                    if (cornerRect.contains(e->globalPos())) {
                        mouseCorner = Utility::BottomRightCorner;

                        goto set_cursor;
                    }

                    cornerRect.moveBottomLeft(window_real_geometry.bottomLeft());

                    if (cornerRect.contains(e->globalPos())) {
                        mouseCorner = Utility::BottomLeftCorner;

                        goto set_cursor;
                    }
set_edge:
                    /// begin set cursor edge type
                    if (e->globalX() <= window_visible_rect.x()) {
                        if (isFixedWidth)
                            goto skip_set_cursor;

                        mouseCorner = Utility::LeftEdge;
                    } else if (e->globalX() < window_visible_rect.right()) {
                        if (isFixedHeight)
                            goto skip_set_cursor;

                        if (e->globalY() <= window_visible_rect.y()) {
                            mouseCorner = Utility::TopEdge;
                        } else if (!isFixedWidth || e->globalY() >= window_visible_rect.bottom()) {
                            mouseCorner = Utility::BottomEdge;
                        } else {
                            goto skip_set_cursor;
                        }
                    } else if (!isFixedWidth && (!isFixedHeight || e->globalX() >= window_visible_rect.right())) {
                        mouseCorner = Utility::RightEdge;
                    } else {
                        goto skip_set_cursor;
                    }
set_cursor:
                    Utility::setWindowCursor(window->winId(), mouseCorner);

                    if (qApp->mouseButtons() == Qt::LeftButton) {
                        Utility::startWindowSystemResize(window->winId(), mouseCorner, e->globalPos());

                        cancelAdsorbCursor();
                    } else {
                        adsorbCursor(mouseCorner);
                    }
                } else if (event->type() == QEvent::MouseButtonRelease) {
                    Utility::cancelWindowMoveResize(window->winId());
                }

                return true;
            }
skip_set_cursor:
            if (e->buttons() == Qt::LeftButton) {
                if (e->type() == QEvent::MouseButtonPress)
                    setLeftButtonPressed(true);
                else if (e->type() == QEvent::MouseButtonRelease)
                    setLeftButtonPressed(false);
            } else {
                setLeftButtonPressed(false);
            }

            qApp->setOverrideCursor(window->cursor());

            cancelAdsorbCursor();
            canAdsorbCursor = true;

            break;
        }
        case QEvent::Resize: {
            DQResizeEvent *e = static_cast<DQResizeEvent*>(event);

            const QRect &rect = QRect(QPoint(0, 0), e->size());

            e->s = (rect - m_store->windowMargins).size();

            break;
        }
        case QEvent::Enter:
            canAdsorbCursor = true;

            break;
        case QEvent::Leave:
            canAdsorbCursor = false;
            cancelAdsorbCursor();

            break;
        case QEvent::DynamicPropertyChange: {
            QDynamicPropertyChangeEvent *e = static_cast<QDynamicPropertyChangeEvent*>(event);

            if (e->propertyName() == netWmStates) {
                m_store->onWindowStateChanged();
            } else if (e->propertyName() == windowRadius) {
                m_store->updateWindowRadius();
            } else if (e->propertyName() == borderWidth) {
                m_store->updateBorderWidth();
            } else if (e->propertyName() == borderColor) {
                m_store->updateBorderColor();
            } else if (e->propertyName() == shadowRadius) {
                m_store->updateShadowRadius();
            } else if (e->propertyName() == shadowOffset) {
                m_store->updateShadowOffset();
            } else if (e->propertyName() == shadowColor) {
                m_store->updateShadowColor();
            } else if (e->propertyName() == clipPath) {
                m_store->updateUserClipPath();
            } else if (e->propertyName() == frameMask) {
                m_store->updateFrameMask();
            }

            break;
        }
        default: break;
        }

        return false;
    }

    void mouseMoveEvent(QMouseEvent *event)
    {
        Q_UNUSED(event);

        Utility::startWindowSystemMove(reinterpret_cast<QWidget*>(this)->winId());
    }

    void timerEvent(QTimerEvent *event) Q_DECL_OVERRIDE
    {
        if (event->timerId() == m_store->updateShadowTimer.timerId()) {
            m_store->repaintWindowShadow();
        }
    }

private:
    void setLeftButtonPressed(bool pressed)
    {
        if (leftButtonPressed == pressed)
            return;

        if (!pressed)
            Utility::cancelWindowMoveResize(m_store->window()->winId());

        leftButtonPressed = pressed;

        const QWidgetWindow *widgetWindow = m_store->widgetWindow();

        QWidget *widget = widgetWindow->widget();

        if (widget) {
            if (pressed) {
                VtableHook::overrideVfptrFun(static_cast<DQWidget*>(widget), &DQWidget::mouseMoveEvent,
                                             this, &WindowEventListener::mouseMoveEvent);
            } else {
                VtableHook::resetVfptrFun(static_cast<DQWidget*>(widget), &DQWidget::mouseMoveEvent);
            }
        } else {
            QWindow *window = m_store->window();

            if (pressed) {
                VtableHook::overrideVfptrFun(static_cast<DQWindow*>(window), &DQWindow::mouseMoveEvent,
                                             this, &WindowEventListener::mouseMoveEvent);
            } else {
                VtableHook::resetVfptrFun(static_cast<DQWindow*>(window), &DQWindow::mouseMoveEvent);
            }
        }
    }

    void adsorbCursor(Utility::CornerEdge cornerEdge)
    {
        lastCornerEdge = cornerEdge;

        if (!canAdsorbCursor)
            return;

        if (cursorAnimation.state() == QVariantAnimation::Running)
            return;

        startAnimationTimer.start();
    }

    void cancelAdsorbCursor()
    {
        QSignalBlocker blocker(&startAnimationTimer);
        Q_UNUSED(blocker)
        startAnimationTimer.stop();
        cursorAnimation.stop();
    }

    void onAnimationValueChanged(const QVariant &value)
    {
        QCursor::setPos(value.toPoint());
    }

    void startAnimation()
    {
        QPoint cursorPos = QCursor::pos();
        QPoint toPos = cursorPos;
        const QRect geometry = m_store->windowValidRect.translated(m_store->window()->position()).adjusted(-1, -1, 1, 1);

        switch (lastCornerEdge) {
        case Utility::TopLeftCorner:
            toPos = geometry.topLeft();
            break;
        case Utility::TopEdge:
            toPos.setY(geometry.y());
            break;
        case Utility::TopRightCorner:
            toPos = geometry.topRight();
            break;
        case Utility::RightEdge:
            toPos.setX(geometry.right());
            break;
        case Utility::BottomRightCorner:
            toPos = geometry.bottomRight();
            break;
        case Utility::BottomEdge:
            toPos.setY(geometry.bottom());
            break;
        case Utility::BottomLeftCorner:
            toPos = geometry.bottomLeft();
            break;
        case Utility::LeftEdge:
            toPos.setX(geometry.x());
            break;
        default:
            break;
        }

        const QPoint &tmp = toPos - cursorPos;

        if (qAbs(tmp.x()) < 3 && qAbs(tmp.y()) < 3)
            return;

        canAdsorbCursor = false;

        cursorAnimation.setStartValue(cursorPos);
        cursorAnimation.setEndValue(toPos);
        cursorAnimation.start();
    }

    /// mouse left button is pressed in window vaild geometry
    bool leftButtonPressed = false;

    bool canAdsorbCursor = false;
    Utility::CornerEdge lastCornerEdge;
    QTimer startAnimationTimer;
    QVariantAnimation cursorAnimation;

    DXcbBackingStore *m_store;
};

class DXcbShmGraphicsBuffer : public QPlatformGraphicsBuffer
{
public:
    DXcbShmGraphicsBuffer(QImage *image)
        : QPlatformGraphicsBuffer(image->size(), QImage::toPixelFormat(image->format()))
        , m_access_lock(QPlatformGraphicsBuffer::None)
        , m_image(image)
    { }

    bool doLock(AccessTypes access, const QRect &rect) Q_DECL_OVERRIDE
    {
        Q_UNUSED(rect);
        if (access & ~(QPlatformGraphicsBuffer::SWReadAccess | QPlatformGraphicsBuffer::SWWriteAccess))
            return false;

        m_access_lock |= access;
        return true;
    }
    void doUnlock() Q_DECL_OVERRIDE { m_access_lock = None; }

    const uchar *data() const Q_DECL_OVERRIDE { return m_image->bits(); }
    uchar *data() Q_DECL_OVERRIDE { return m_image->bits(); }
    int bytesPerLine() const Q_DECL_OVERRIDE { return m_image->bytesPerLine(); }

    Origin origin() const Q_DECL_OVERRIDE { return QPlatformGraphicsBuffer::OriginTopLeft; }

private:
    AccessTypes m_access_lock;
    QImage *m_image;
};

DXcbBackingStore::DXcbBackingStore(QWindow *window, QXcbBackingStore *proxy)
    : QPlatformBackingStore(window)
    , m_proxy(proxy)
    , m_eventListener(new WindowEventListener(this))
{
    shadowPixmap.fill(Qt::transparent);

    initUserPropertys();

    //! Warning: At this point you must be initialized window Margins and window Extents
    updateWindowMargins();
    updateFrameExtents();

    VtableHook::overrideVfptrFun(static_cast<QXcbWindow*>(window->handle()), &QXcbWindowEventListener::handlePropertyNotifyEvent,
                                 this, &DXcbBackingStore::handlePropertyNotifyEvent);

    QObject::connect(window, &QWindow::windowStateChanged,
                     m_eventListener, [window, this] {
        updateWindowMargins(false);
    });
}

DXcbBackingStore::~DXcbBackingStore()
{
    delete m_proxy;
    delete m_eventListener;

    if (m_graphicsBuffer)
        delete m_graphicsBuffer;
}

QPaintDevice *DXcbBackingStore::paintDevice()
{
    return &m_image;
}

void DXcbBackingStore::flush(QWindow *window, const QRegion &region, const QPoint &offset)
{
    Q_UNUSED(region)

    const QPoint &windowOffset = this->windowOffset();
    QRegion tmp_region;

//    qDebug() << "flush" << window << tmp_region << offset;

    QPainter pa(m_proxy->paintDevice());

    pa.setCompositionMode(QPainter::CompositionMode_Source);
    pa.setRenderHint(QPainter::Antialiasing);
    pa.setClipPath(m_windowClipPath);

    for (const QRect &rect : region.rects()) {
        const QRect &tmp_rect = rect.translated(windowOffset);

        pa.drawImage(tmp_rect, m_image, rect);
        tmp_region += tmp_rect;
    }

    pa.end();

    XcbWindowHook *window_hook = XcbWindowHook::getHookByWindow(window->handle());

    if (window_hook)
        window_hook->windowMargins = QMargins(0, 0, 0, 0);

    m_proxy->flush(window, tmp_region, offset);

    if (window_hook)
        window_hook->windowMargins = windowMargins;
}

void DXcbBackingStore::composeAndFlush(QWindow *window, const QRegion &region, const QPoint &offset,
                                       QPlatformTextureList *textures, QOpenGLContext *context,
                                       bool translucentBackground)
{
    Q_UNUSED(textures);
    Q_UNUSED(context);
    Q_UNUSED(translucentBackground)

    flush(window, region, offset);
}

QImage DXcbBackingStore::toImage() const
{
    return m_image;
}

GLuint DXcbBackingStore::toTexture(const QRegion &dirtyRegion, QSize *textureSize, TextureFlags *flags) const
{
    return m_proxy->toTexture(dirtyRegion, textureSize, flags);
}

QPlatformGraphicsBuffer *DXcbBackingStore::graphicsBuffer() const
{
    return m_graphicsBuffer;
}

void DXcbBackingStore::resize(const QSize &size, const QRegion &staticContents)
{
//    qDebug() << "resize" << size << staticContents;

    const int dpr = int(window()->devicePixelRatio());
    const QSize xSize = size * dpr;
    if (xSize == m_image.size() && dpr == m_image.devicePixelRatio())
        return;

    if (m_graphicsBuffer)
        delete m_graphicsBuffer;

    m_image = QImage(xSize, QImage::Format_RGB32);
    m_image.setDevicePixelRatio(dpr);
//    m_image.fill(Qt::transparent);
    // Slow path for bgr888 VNC: Create an additional image, paint into that and
    // swap R and B while copying to m_image after each paint.

    m_graphicsBuffer = new DXcbShmGraphicsBuffer(&m_image);

    m_size = QSize(size.width() + windowMargins.left() + windowMargins.right(),
                   size.height() + windowMargins.top() + windowMargins.bottom());

    m_proxy->resize(m_size, staticContents);

    updateClipPath();
    //! TODO: update window margins
    //    updateWindowMargins();

    if (!isUserSetClipPath || shadowPixmap.isNull()) {
        updateInputShapeRegion();
        updateWindowShadow();
    }

    paintWindowShadow();
}

void DXcbBackingStore::beginPaint(const QRegion &region)
{
    if (m_image.hasAlphaChannel()) {
        QPainter p(paintDevice());
        p.setCompositionMode(QPainter::CompositionMode_Source);
        const QVector<QRect> rects = region.rects();
        const QColor blank = Qt::transparent;
        for (QVector<QRect>::const_iterator it = rects.begin(); it != rects.end(); ++it) {
            const QRect &rect = it->translated(windowOffset());
            p.fillRect(rect, blank);
        }
    }
}

void DXcbBackingStore::endPaint()
{
//    m_proxy->endPaint();

//    qDebug() << "end paint";
}

void DXcbBackingStore::initUserPropertys()
{
    updateWindowRadius();
    updateBorderWidth();
    updateBorderColor();
    updateUserClipPath();
    updateFrameMask();
    updateShadowRadius();
    updateShadowOffset();
    updateShadowColor();
}

void DXcbBackingStore::updateWindowMargins(bool repaintShadow)
{
    Qt::WindowState state = window()->windowState();

    const QMargins old_margins = windowMargins;
    const QRect &window_geometry = window()->geometry();

    if (state == Qt::WindowMaximized || state == Qt::WindowFullScreen) {
        setWindowMargins(QMargins(0, 0, 0, 0));
    } else {
        setWindowMargins(QMargins(m_shadowRadius - m_shadowOffset.x(),
                                  m_shadowRadius - m_shadowOffset.y(),
                                  m_shadowRadius + m_shadowOffset.x(),
                                  m_shadowRadius + m_shadowOffset.y()));
    }

    if (repaintShadow && old_margins != windowMargins) {
        window()->setGeometry(window_geometry);

        repaintWindowShadow();
    }
}

void DXcbBackingStore::updateFrameExtents()
{
    const QMargins &borderMargins = QMargins(m_borderWidth, m_borderWidth, m_borderWidth, m_borderWidth);

    QMargins extentsMargins = windowMargins;

    if (canUseClipPath() && !isUserSetClipPath) {
        extentsMargins -= borderMargins;
    }

    Utility::setFrameExtents(window()->winId(), extentsMargins);
}

void DXcbBackingStore::updateInputShapeRegion()
{
    if (isUserSetClipPath)
        return;

    QRegion region(windowGeometry().adjusted(-MOUSE_MARGINS, -MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS));

    Utility::setInputShapeRectangles(window()->winId(), region);
}

void DXcbBackingStore::updateWindowRadius()
{
    const QVariant &v = window()->property(windowRadius);

    bool ok;
    int radius = v.toInt(&ok);

    if (ok && radius != m_windowRadius) {
        m_windowRadius = radius;

        updateClipPath();
    }
}

void DXcbBackingStore::updateBorderWidth()
{
    const QVariant &v = window()->property(borderWidth);

    bool ok;
    int width = v.toInt(&ok);

    if (ok && width != m_borderWidth) {
        m_borderWidth = width;

        updateFrameExtents();
        doDelayedUpdateWindowShadow();
    }
}

void DXcbBackingStore::updateBorderColor()
{
    const QVariant &v = window()->property(borderColor);
    const QColor &color = qvariant_cast<QColor>(v);

    if (color.isValid() && m_borderColor != color) {
        m_borderColor = color;

        doDelayedUpdateWindowShadow();
    }
}

void DXcbBackingStore::updateUserClipPath()
{
    const QVariant &v = window()->property(clipPath);
    QPainterPath path;

    if (v.isValid()) {
        path = qvariant_cast<QPainterPath>(v);
    }

    if (!isUserSetClipPath && path.isEmpty())
        return;

    isUserSetClipPath = !path.isEmpty();

    if (path.isEmpty())
        updateClipPath();
    else
        setClipPah(path);
}

void DXcbBackingStore::updateClipPath()
{
    if (!isUserSetClipPath) {
        QPainterPath path;

        if (canUseClipPath())
            path.addRoundedRect(QRect(QPoint(0, 0), m_image.size()), m_windowRadius, m_windowRadius);
        else
            path.addRect(0, 0, m_image.width(), m_image.height());

        setClipPah(path);
    }
}

void DXcbBackingStore::updateFrameMask()
{
    const QVariant &v = window()->property(frameMask);

    if (!v.isValid())
        return;

    QRegion region = qvariant_cast<QRegion>(v);

    static_cast<QXcbWindow*>(window()->handle())->QXcbWindow::setMask(region);

    isUserSetFrameMask = !region.isEmpty();
}

void DXcbBackingStore::updateShadowRadius()
{
    const QVariant &v = window()->property(shadowRadius);

    bool ok;
    int radius = v.toInt(&ok);

    if (ok && radius != m_shadowRadius) {
        m_shadowRadius = radius;

        updateWindowMargins();
        doDelayedUpdateWindowShadow();
    }
}

void DXcbBackingStore::updateShadowOffset()
{
    const QVariant &v = window()->property(shadowOffset);
    const QPoint &offset = v.toPoint();

    if (!offset.isNull() && offset != m_shadowOffset) {
        m_shadowOffset = offset;

        updateWindowMargins();
        doDelayedUpdateWindowShadow();
    }
}

void DXcbBackingStore::updateShadowColor()
{
    const QVariant &v = window()->property(shadowColor);
    const QColor &color = qvariant_cast<QColor>(v);

    if (color.isValid() && m_shadowColor != color) {
        m_shadowColor = color;

        doDelayedUpdateWindowShadow();
    }
}

void DXcbBackingStore::setWindowMargins(const QMargins &margins)
{
    if (windowMargins == margins)
        return;

    windowMargins = margins;
    m_windowClipPath = m_clipPath.translated(windowOffset());

    XcbWindowHook *hook = XcbWindowHook::getHookByWindow(m_proxy->window()->handle());

    if (hook) {
        hook->windowMargins = margins;
    }

    const QSize &tmp_size = m_image.size();

    m_size = QSize(tmp_size.width() + windowMargins.left() + windowMargins.right(),
                   tmp_size.height() + windowMargins.top() + windowMargins.bottom());

    m_proxy->resize(m_size, QRegion());

    updateInputShapeRegion();
    updateFrameExtents();
}

void DXcbBackingStore::setClipPah(const QPainterPath &path)
{
    if (m_clipPath != path) {
        m_clipPath = path;
        m_windowClipPath = m_clipPath.translated(windowOffset());
        windowValidRect = m_clipPath.boundingRect().toRect();

        if (isUserSetClipPath) {
            doDelayedUpdateWindowShadow();
        }
    }
}

void DXcbBackingStore::paintWindowShadow(QRegion region)
{
    QPainter pa;

    /// begin paint window drop shadow
    pa.begin(m_proxy->paintDevice());
    pa.setCompositionMode(QPainter::CompositionMode_Source);
    pa.drawPixmap(0, 0, shadowPixmap);
    pa.end();

    XcbWindowHook *window_hook = XcbWindowHook::getHookByWindow(window()->handle());

    if (window_hook)
        window_hook->windowMargins = QMargins(0, 0, 0, 0);

    if (region.isEmpty()) {
        region += QRect(windowMargins.left(), 0, m_size.width(), windowMargins.top());
        region += QRect(0, 0, windowOffset().x(), m_size.height());
    }

    m_proxy->flush(window(), region, QPoint(0, 0));

    if (window_hook)
        window_hook->windowMargins = windowMargins;
    /// end
}

void DXcbBackingStore::repaintWindowShadow()
{
    updateShadowTimer.stop();

    shadowPixmap = QPixmap();

    updateWindowShadow();
    paintWindowShadow(QRegion(0, 0, m_size.width(), m_size.height()));

    flush(window(), QRect(QPoint(0, 0), m_image.size()), QPoint(0, 0));
}

inline QSize margins2Size(const QMargins &margins)
{
    return QSize(margins.left() + margins.right(),
                 margins.top() + margins.bottom());
}

void DXcbBackingStore::updateWindowShadow()
{
    QPixmap pixmap(m_image.size());

    if (pixmap.isNull())
        return;

    pixmap.fill(Qt::transparent);

    QPainter pa(&pixmap);

    pa.fillPath(m_clipPath, m_shadowColor);
    pa.end();

    bool paintShadow = isUserSetClipPath || shadowPixmap.isNull();

    if (!paintShadow) {
        QSize margins_size = margins2Size(windowMargins + m_windowRadius + m_borderWidth);

        if (margins_size.width() > qMin(m_size.width(), shadowPixmap.width())
                || margins_size.height() > qMin(m_size.height(), shadowPixmap.height())) {
            paintShadow = true;
        }
    }

    if (paintShadow) {
        QImage image = Utility::dropShadow(pixmap, m_shadowRadius, m_shadowColor);

        /// begin paint window border;
        QPainter pa(&image);
        QPainterPathStroker pathStroker;

        pathStroker.setWidth(m_borderWidth * 2);

        QTransform transform = pa.transform();
        const QRectF &clipRect = m_clipPath.boundingRect();

        transform.translate(windowMargins.left() + 2, windowMargins.top() + 2);
        transform.scale((clipRect.width() - 4) / clipRect.width(),
                        (clipRect.height() - 4) / clipRect.height());

        pa.setCompositionMode(QPainter::CompositionMode_Source);
        pa.setRenderHint(QPainter::Antialiasing);
        pa.fillPath(pathStroker.createStroke(m_windowClipPath), m_borderColor);
        pa.setCompositionMode(QPainter::CompositionMode_Clear);
        pa.setRenderHint(QPainter::Antialiasing, false);
        pa.setTransform(transform);
        pa.fillPath(m_clipPath, Qt::transparent);
        pa.end();
        /// end

        shadowPixmap = QPixmap::fromImage(image);
    } else {
        shadowPixmap = QPixmap::fromImage(Utility::borderImage(shadowPixmap, windowMargins + m_windowRadius, m_size));
    }
}

void DXcbBackingStore::doDelayedUpdateWindowShadow(int delaye)
{
    updateShadowTimer.start(delaye, m_eventListener);
}

bool DXcbBackingStore::isWidgetWindow(const QWindow *window)
{
    return window->metaObject()->className() == QStringLiteral("QWidgetWindow");
}

QWidgetWindow *DXcbBackingStore::widgetWindow() const
{
    return static_cast<QWidgetWindow*>(window());
}

bool DXcbBackingStore::canUseClipPath() const
{
    QXcbWindow::NetWmStates states = (QXcbWindow::NetWmStates)window()->property(netWmStates).toInt();

    if (states & (QXcbWindow::NetWmStateFullScreen | QXcbWindow::NetWmStateMaximizedHorz | QXcbWindow::NetWmStateMaximizedVert)) {
        return false;
    }

    return true;
}

void DXcbBackingStore::onWindowStateChanged()
{
    updateClipPath();
    updateFrameExtents();
    doDelayedUpdateWindowShadow();
}

void DXcbBackingStore::handlePropertyNotifyEvent(const xcb_property_notify_event_t *event)
{
    DQXcbWindow *window = static_cast<DQXcbWindow*>(reinterpret_cast<QXcbWindowEventListener*>(this));

    window->QXcbWindow::handlePropertyNotifyEvent(event);

    if (event->window == window->xcb_window()
            && event->atom == window->connection()->internAtom("_NET_WM_STATE")) {
        QXcbWindow::NetWmStates states = window->netWmStates();

        window->window()->setProperty(netWmStates, (int)states);

        QWindow *ww = window->window();

        switch (states) {
        case 0:
            if (ww->windowState() != Qt::WindowNoState)
                ww->setWindowState(Qt::WindowNoState);
            break;
        case QXcbWindow::NetWmStateFullScreen:
            if (ww->windowState() != Qt::WindowFullScreen)
                ww->setWindowState(Qt::WindowFullScreen);
            break;
        case QXcbWindow::NetWmStateMaximizedHorz | QXcbWindow::NetWmStateMaximizedVert:
            if (ww->windowState() != Qt::WindowMaximized)
                ww->setWindowState(Qt::WindowMaximized);
            break;
        default:
            break;
        }
    }
}
