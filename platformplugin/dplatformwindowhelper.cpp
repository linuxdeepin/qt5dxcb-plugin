/*
 * Copyright (C) 2017 ~ 2017 Deepin Technology Co., Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dplatformwindowhelper.h"
#include "dplatformintegration.h"
#include "dframewindow.h"
#include "vtablehook.h"
#include "dwmsupport.h"

#ifdef Q_OS_LINUX
#include "qxcbwindow.h"
#endif

#include <private/qwindow_p.h>
#include <private/qguiapplication_p.h>
#include <qpa/qplatformcursor.h>

Q_DECLARE_METATYPE(QPainterPath)
Q_DECLARE_METATYPE(QMargins)

DPP_BEGIN_NAMESPACE

#define HOOK_VFPTR(Fun) VtableHook::overrideVfptrFun(window, &QPlatformWindow::Fun, this, &DPlatformWindowHelper::Fun)
#define CALL this->window()->QNativeWindow

PUBLIC_CLASS(QWindow, DPlatformWindowHelper);
PUBLIC_CLASS(QMouseEvent, DPlatformWindowHelper);
PUBLIC_CLASS(QDropEvent, DPlatformWindowHelper);
PUBLIC_CLASS(QNativeWindow, DPlatformWindowHelper);

QHash<const QPlatformWindow*, DPlatformWindowHelper*> DPlatformWindowHelper::mapped;

DPlatformWindowHelper::DPlatformWindowHelper(QNativeWindow *window)
    : QObject(window->window())
    , m_nativeWindow(window)
{
    mapped[window] = this;

    m_frameWindow = new DFrameWindow();
    m_frameWindow->setFlags((window->window()->flags() | Qt::FramelessWindowHint | Qt::CustomizeWindowHint | Qt::NoDropShadowWindowHint) & ~Qt::WindowMinMaxButtonsHint);
    m_frameWindow->create();
    m_frameWindow->installEventFilter(this);
    m_frameWindow->setShadowRadius(getShadowRadius());
    m_frameWindow->setShadowColor(m_shadowColor);
    m_frameWindow->setShadowOffset(m_shadowOffset);
    m_frameWindow->setBorderWidth(m_borderWidth);
    m_frameWindow->setBorderColor(getBorderColor());
    m_frameWindow->setEnableSystemMove(m_enableSystemMove);
    m_frameWindow->setEnableSystemResize(m_enableSystemResize);

    m_frameWindow->m_contentWindow = window->window();
    m_frameWindow->setProperty("_d_real_content_window", (quintptr)m_frameWindow->m_contentWindow.data());

    window->setParent(m_frameWindow->handle());
    window->window()->installEventFilter(this);
    updateClipPathByWindowRadius(window->window()->size());

    updateClipPathFromProperty();
    updateFrameMaskFromProperty();
    updateWindowRadiusFromProperty();
    updateBorderWidthFromProperty();
    updateBorderColorFromProperty();
    updateShadowRadiusFromProperty();
    updateShadowOffsetFromProperty();
    updateShadowColorFromProperty();
    updateEnableSystemResizeFromProperty();
    updateEnableSystemMoveFromProperty();
    updateEnableBlurWindowFromProperty();
    updateWindowBlurAreasFromProperty();
    updateWindowBlurPathsFromProperty();
    updateAutoInputMaskByClipPathFromProperty();

    HOOK_VFPTR(setGeometry);
    HOOK_VFPTR(geometry);
    HOOK_VFPTR(normalGeometry);
    HOOK_VFPTR(frameMargins);
    HOOK_VFPTR(setVisible);
    HOOK_VFPTR(setWindowFlags);
    HOOK_VFPTR(setWindowState);
    HOOK_VFPTR(winId);
    HOOK_VFPTR(setParent);
    HOOK_VFPTR(setWindowTitle);
    HOOK_VFPTR(setWindowFilePath);
    HOOK_VFPTR(setWindowIcon);
    HOOK_VFPTR(raise);
    HOOK_VFPTR(lower);
//    HOOK_VFPTR(isExposed);
    HOOK_VFPTR(isEmbedded);
    HOOK_VFPTR(propagateSizeHints);
    HOOK_VFPTR(requestActivateWindow);
//    HOOK_VFPTR(setKeyboardGrabEnabled);
//    HOOK_VFPTR(setMouseGrabEnabled);
    HOOK_VFPTR(setWindowModified);
//    HOOK_VFPTR(windowEvent);
    HOOK_VFPTR(startSystemResize);
    HOOK_VFPTR(setFrameStrutEventsEnabled);
    HOOK_VFPTR(frameStrutEventsEnabled);
    HOOK_VFPTR(setAlertState);
    HOOK_VFPTR(isAlertState);

    connect(m_frameWindow, &DFrameWindow::contentMarginsHintChanged,
            this, &DPlatformWindowHelper::onFrameWindowContentMarginsHintChanged);
    connect(DWMSupport::instance(), &DXcbWMSupport::hasCompositeChanged,
            this, &DPlatformWindowHelper::onWMHasCompositeChanged);
    connect(DWMSupport::instance(), &DXcbWMSupport::windowManagerChanged,
            this, &DPlatformWindowHelper::updateWindowBlurAreasForWM);

    static_cast<QPlatformWindow*>(window)->propagateSizeHints();
}

DPlatformWindowHelper::~DPlatformWindowHelper()
{
    mapped.remove(m_nativeWindow);
    VtableHook::clearGhostVtable(static_cast<QPlatformWindow*>(m_nativeWindow));

    m_frameWindow->deleteLater();
}

DPlatformWindowHelper *DPlatformWindowHelper::me() const
{
    return DPlatformWindowHelper::mapped.value(window());
}

void DPlatformWindowHelper::setGeometry(const QRect &rect)
{
    DPlatformWindowHelper *helper = me();

    bool position_automatic = qt_window_private(helper->m_nativeWindow->window())->positionAutomatic;
    QWindowPrivate::PositionPolicy position_policy = qt_window_private(helper->m_nativeWindow->window())->positionPolicy;
    qreal device_pixel_ratio = helper->m_frameWindow->devicePixelRatio();

    // update clip path
    helper->updateClipPathByWindowRadius(rect.size() / device_pixel_ratio);

    qt_window_private(helper->m_nativeWindow->window())->positionAutomatic = false;

    const QMargins &content_margins = helper->m_frameWindow->contentMarginsHint() * device_pixel_ratio;
    const QPoint &content_offset = helper->m_frameWindow->contentOffsetHint() * device_pixel_ratio;

    if (!helper->overrideSetGeometry) {
        helper->m_nativeWindow->QNativeWindow::setGeometry(QRect(content_offset, rect.size()));
        // reset
        qt_window_private(helper->m_nativeWindow->window())->positionAutomatic = position_automatic;
        qt_window_private(helper->m_nativeWindow->window())->positionPolicy = position_policy;
        return;
    }

    // save new size
    helper->m_frameWindowSize =(rect + content_margins).size() / device_pixel_ratio;

    qt_window_private(helper->m_frameWindow)->positionAutomatic = position_automatic;
    qt_window_private(helper->m_frameWindow)->positionPolicy = position_policy;
    helper->m_frameWindow->handle()->setGeometry(rect + content_margins);
    helper->m_nativeWindow->QNativeWindow::setGeometry(QRect(content_offset, rect.size()));
    // reset
    qt_window_private(helper->m_nativeWindow->window())->positionAutomatic = position_automatic;
    qt_window_private(helper->m_nativeWindow->window())->positionPolicy = position_policy;
}

QRect DPlatformWindowHelper::geometry() const
{
    DPlatformWindowHelper *helper = me();
    const QRect &geometry = helper->m_frameWindow->handle()->geometry();

    if (geometry.topLeft() == QPoint(0, 0) && geometry.size() == QSize(0, 0))
        return geometry;

    QRect rect = geometry - helper->m_frameWindow->contentMarginsHint() * helper->m_frameWindow->devicePixelRatio();

    rect.setSize(helper->m_nativeWindow->QNativeWindow::geometry().size());

    return rect;
}

QRect DPlatformWindowHelper::normalGeometry() const
{
    return me()->m_frameWindow->handle()->normalGeometry();
}

QMargins DPlatformWindowHelper::frameMargins() const
{
    return me()->m_frameWindow->handle()->frameMargins();
}

QWindow *topvelWindow(QWindow *w)
{
    QWindow *tw = w;

    while (tw->parent())
        tw = tw->parent();

    DPlatformWindowHelper *helper = DPlatformWindowHelper::mapped.value(tw->handle());

    return helper ? helper->m_frameWindow : tw;
}

void DPlatformWindowHelper::setVisible(bool visible)
{
    DPlatformWindowHelper *helper = me();

    if (visible) {
        QWindow *tp = helper->m_nativeWindow->window()->transientParent();
        helper->m_nativeWindow->window()->setTransientParent(helper->m_frameWindow);

        if (tp) {
            QWindow *tw = topvelWindow(tp);

            if (tw != helper->m_frameWindow)
                helper->m_frameWindow->setTransientParent(tw);
        }

#ifdef Q_OS_LINUX
        // reupdate _MOTIF_WM_HINTS
        DQNativeWindow *window = static_cast<DQNativeWindow*>(helper->m_frameWindow->handle());

        Utility::QtMotifWmHints mwmhints = Utility::getMotifWmHints(window->m_window);

        if (window->window()->modality() != Qt::NonModal) {
            switch (window->window()->modality()) {
            case Qt::WindowModal:
                mwmhints.input_mode = DXcbWMSupport::MWM_INPUT_PRIMARY_APPLICATION_MODAL;
                break;
            case Qt::ApplicationModal:
            default:
                mwmhints.input_mode = DXcbWMSupport::MWM_INPUT_FULL_APPLICATION_MODAL;
                break;
            }
            mwmhints.flags |= DXcbWMSupport::MWM_HINTS_INPUT_MODE;
        } else {
            mwmhints.input_mode = DXcbWMSupport::MWM_INPUT_MODELESS;
            mwmhints.flags &= ~DXcbWMSupport::MWM_HINTS_INPUT_MODE;
        }

        QWindow *content_window = helper->m_nativeWindow->window();
        Utility::QtMotifWmHints cw_hints = Utility::getMotifWmHints(helper->m_nativeWindow->QNativeWindow::winId());
        bool size_fixed = content_window->minimumSize() == content_window->maximumSize();

        if (size_fixed) {
            // fixed size, remove the resize handle (since mwm/dtwm
            // isn't smart enough to do it itself)
            mwmhints.flags |= DXcbWMSupport::MWM_HINTS_FUNCTIONS;
            if (mwmhints.functions & DXcbWMSupport::MWM_FUNC_ALL) {
                mwmhints.functions = DXcbWMSupport::MWM_FUNC_MOVE;
            } else {
                mwmhints.functions &= ~DXcbWMSupport::MWM_FUNC_RESIZE;
            }

            if (mwmhints.decorations & DXcbWMSupport::MWM_DECOR_ALL) {
                mwmhints.flags |= DXcbWMSupport::MWM_HINTS_DECORATIONS;
                mwmhints.decorations = (DXcbWMSupport::MWM_DECOR_BORDER
                                        | DXcbWMSupport::MWM_DECOR_TITLE
                                        | DXcbWMSupport::MWM_DECOR_MENU);
            } else {
                mwmhints.decorations &= ~DXcbWMSupport::MWM_DECOR_RESIZEH;
            }

            // set content window decoration hints
            cw_hints.flags |= DXcbWMSupport::MWM_HINTS_DECORATIONS;
            cw_hints.decorations = DXcbWMSupport::MWM_DECOR_MINIMIZE;
        }

        if (content_window->flags() & Qt::WindowMinimizeButtonHint) {
            mwmhints.functions |= DXcbWMSupport::MWM_FUNC_MINIMIZE;

            cw_hints.decorations |= DXcbWMSupport::MWM_DECOR_MINIMIZE;
        }
        if (content_window->flags() & Qt::WindowMaximizeButtonHint) {
            mwmhints.functions |= DXcbWMSupport::MWM_FUNC_MAXIMIZE;

            if (!size_fixed)
                cw_hints.decorations |= DXcbWMSupport::MWM_DECOR_MAXIMIZE;
        }
        if (content_window->flags() & Qt::WindowCloseButtonHint) {
            mwmhints.functions |= DXcbWMSupport::MWM_FUNC_CLOSE;
        }
        if (content_window->flags() & Qt::WindowTitleHint) {
            cw_hints.decorations |= DXcbWMSupport::MWM_DECOR_TITLE;
        }
        if (content_window->flags() & Qt::WindowSystemMenuHint) {
            cw_hints.decorations |= DXcbWMSupport::MWM_DECOR_MENU;
        }
#endif

        helper->m_frameWindow->setVisible(visible);
        helper->m_nativeWindow->QNativeWindow::setVisible(visible);
        helper->updateWindowBlurAreasForWM();

        // restore
        if (tp)
            helper->m_nativeWindow->window()->setTransientParent(tp);

#ifdef Q_OS_LINUX
        // Fix the window can't show minimized if window is fixed size
        Utility::setMotifWmHints(window->m_window, mwmhints);
        Utility::setMotifWmHints(helper->m_nativeWindow->QNativeWindow::winId(), cw_hints);
#endif

        return;
    }

    helper->m_frameWindow->setVisible(visible);
    helper->m_nativeWindow->QNativeWindow::setVisible(visible);
}

void DPlatformWindowHelper::setWindowFlags(Qt::WindowFlags flags)
{
    me()->m_frameWindow->setFlags((flags | Qt::FramelessWindowHint | Qt::CustomizeWindowHint | Qt::NoDropShadowWindowHint) & ~Qt::WindowMinMaxButtonsHint);
    window()->QNativeWindow::setWindowFlags(flags);
}

void DPlatformWindowHelper::setWindowState(Qt::WindowState state)
{
#ifdef Q_OS_LINUX
    DQNativeWindow *window = static_cast<DQNativeWindow*>(me()->m_frameWindow->handle());

    if (window->m_windowState == state)
        return;

    if (state == Qt::WindowMinimized
            && (window->m_windowState == Qt::WindowMaximized
                || window->m_windowState == Qt::WindowFullScreen)) {
        window->changeNetWmState(true, Utility::internAtom("_NET_WM_STATE_HIDDEN"));
        Utility::XIconifyWindow(window->connection()->xlib_display(),
                                window->m_window,
                                window->connection()->primaryScreenNumber());
        window->connection()->sync();
        window->m_windowState = state;
    } else
#endif
    {
        me()->m_frameWindow->setWindowState(state);
    }
}

WId DPlatformWindowHelper::winId() const
{
    return me()->m_frameWindow->handle()->winId();
}

void DPlatformWindowHelper::setParent(const QPlatformWindow *window)
{
    me()->m_frameWindow->handle()->setParent(window);
}

void DPlatformWindowHelper::setWindowTitle(const QString &title)
{
    me()->m_frameWindow->handle()->setWindowTitle(title);
}

void DPlatformWindowHelper::setWindowFilePath(const QString &title)
{
    me()->m_frameWindow->handle()->setWindowFilePath(title);
}

void DPlatformWindowHelper::setWindowIcon(const QIcon &icon)
{
    me()->m_frameWindow->handle()->setWindowIcon(icon);
}

void DPlatformWindowHelper::raise()
{
    me()->m_frameWindow->handle()->raise();
}

void DPlatformWindowHelper::lower()
{
    me()->m_frameWindow->handle()->lower();
}

bool DPlatformWindowHelper::isExposed() const
{
    return me()->m_frameWindow->handle()->isExposed();
}

#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
bool DPlatformWindowHelper::isEmbedded() const
{
    return me()->m_frameWindow->handle()->isEmbedded();
}
#else
bool DPlatformWindowHelper::isEmbedded(const QPlatformWindow *parentWindow) const
{
    return me()->m_frameWindow->handle()->isEmbedded(parentWindow);
}
#endif

void DPlatformWindowHelper::propagateSizeHints()
{
    me()->updateSizeHints();

    const QWindow *window = this->window()->window();

    if (window->maximumSize() == window->minimumSize()) {
        Utility::QtMotifWmHints cw_hints = Utility::getMotifWmHints(this->window()->QNativeWindow::winId());

        cw_hints.flags |= DXcbWMSupport::MWM_HINTS_DECORATIONS;
        cw_hints.decorations = DXcbWMSupport::MWM_DECOR_MINIMIZE;

        if (window->flags() & Qt::WindowTitleHint) {
            cw_hints.decorations |= DXcbWMSupport::MWM_DECOR_TITLE;
        }
        if (window->flags() & Qt::WindowSystemMenuHint) {
            cw_hints.decorations |= DXcbWMSupport::MWM_DECOR_MENU;
        }

        Utility::setMotifWmHints(this->window()->QNativeWindow::winId(), cw_hints);
    }
}

void DPlatformWindowHelper::requestActivateWindow()
{
    DPlatformWindowHelper *helper = me();

    if (helper->m_nativeWindow->window()->isActive())
        return;

#ifdef Q_OS_LINUX
    if (helper->m_frameWindow->handle()->isExposed() && !DXcbWMSupport::instance()->hasComposite()
            && helper->m_frameWindow->windowState() == Qt::WindowMinimized) {
        Q_XCB_CALL(xcb_map_window(DPlatformIntegration::xcbConnection()->xcb_connection(), helper->m_frameWindow->winId()));
    }
#endif

    helper->m_frameWindow->handle()->requestActivateWindow();
}

bool DPlatformWindowHelper::setKeyboardGrabEnabled(bool grab)
{
    return me()->m_frameWindow->handle()->setKeyboardGrabEnabled(grab);
}

bool DPlatformWindowHelper::setMouseGrabEnabled(bool grab)
{
    return me()->m_frameWindow->handle()->setMouseGrabEnabled(grab);
}

bool DPlatformWindowHelper::setWindowModified(bool modified)
{
    return me()->m_frameWindow->handle()->setWindowModified(modified);
}

bool DPlatformWindowHelper::startSystemResize(const QPoint &pos, Qt::Corner corner)
{
    return me()->m_frameWindow->handle()->startSystemResize(pos, corner);
}

void DPlatformWindowHelper::setFrameStrutEventsEnabled(bool enabled)
{
    me()->m_frameWindow->handle()->setFrameStrutEventsEnabled(enabled);
}

bool DPlatformWindowHelper::frameStrutEventsEnabled() const
{
    return me()->m_frameWindow->handle()->frameStrutEventsEnabled();
}

void DPlatformWindowHelper::setAlertState(bool enabled)
{
    me()->m_frameWindow->handle()->setAlertState(enabled);
}

bool DPlatformWindowHelper::isAlertState() const
{
    return me()->m_frameWindow->handle()->isAlertState();
}

bool DPlatformWindowHelper::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_frameWindow) {
        switch ((int)event->type()) {
        case QEvent::Close:
            m_nativeWindow->window()->close();
            break;
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::Move:
        case QEvent::WindowDeactivate:
            QCoreApplication::sendEvent(m_nativeWindow->window(), event);
            return true;
        case QEvent::FocusIn:
            QWindowSystemInterface::handleWindowActivated(m_nativeWindow->window(), static_cast<QFocusEvent*>(event)->reason());
            return true;
        case QEvent::WindowActivate:
            QWindowSystemInterface::handleWindowActivated(m_nativeWindow->window(), Qt::OtherFocusReason);
            return true;
        case QEvent::Resize: {
            const QResizeEvent *e = static_cast<QResizeEvent*>(event);

            if (m_frameWindowSize == e->size()) {
                break;
            } else {
                m_frameWindowSize = e->size();
            }

            const QMargins &margins = m_frameWindow->contentMarginsHint();
            const QSize size_dif(margins.left() + margins.right(), margins.top() + margins.bottom());

            QResizeEvent new_e(e->size() - size_dif, e->oldSize() - size_dif);

            overrideSetGeometry = false;
            m_nativeWindow->window()->resize(new_e.size());
            overrideSetGeometry = true;
            qApp->sendEvent(m_nativeWindow->window(), &new_e);

            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease: {
            DQMouseEvent *e = static_cast<DQMouseEvent*>(event);

            if (m_windowVaildGeometry.contains(e->pos() - m_frameWindow->contentOffsetHint())) {
                e->l = e->w = m_nativeWindow->window()->mapFromGlobal(e->globalPos());
                qApp->sendEvent(m_nativeWindow->window(), e);

                return true;
            }

            break;
        }
        case QEvent::WindowStateChange:
            qt_window_private(m_nativeWindow->window())->windowState = m_frameWindow->windowState();
            QCoreApplication::sendEvent(m_nativeWindow->window(), event);
            break;
        case QEvent::DragEnter:
        case QEvent::DragMove:
        case QEvent::DragLeave:
        case QEvent::Drop: {
            DQDropEvent *e = static_cast<DQDropEvent*>(event);
            e->p -= m_frameWindow->contentOffsetHint();
            QCoreApplication::sendEvent(m_nativeWindow->window(), event);
            return true;
        }
        case QEvent::PlatformSurface: {
            const QPlatformSurfaceEvent *e = static_cast<QPlatformSurfaceEvent*>(event);

            if (e->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
                m_nativeWindow->window()->destroy();

            break;
        }
        default: break;
        }
    } else if (watched == m_nativeWindow->window()) {
        switch ((int)event->type()) {
        case QEvent::MouseMove: {
            if (qApp->mouseButtons() != Qt::LeftButton)
                break;

            static QEvent *last_event = NULL;

            if (last_event == event) {
                last_event = NULL;

                return false;
            }

            last_event = event;
            QCoreApplication::sendEvent(watched, event);

            if (!event->isAccepted()) {
                DQMouseEvent *e = static_cast<DQMouseEvent*>(event);

                e->l = e->w = m_frameWindow->mapFromGlobal(e->globalPos());
                QGuiApplicationPrivate::setMouseEventSource(e, Qt::MouseEventSynthesizedByQt);
                m_frameWindow->mouseMoveEvent(e);
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            if (m_frameWindow->m_isSystemMoveResizeState) {
                Utility::cancelWindowMoveResize(Utility::getNativeTopLevelWindow(m_frameWindow->winId()));
                m_frameWindow->m_isSystemMoveResizeState = false;
            }

            break;
        }
        case QEvent::PlatformSurface: {
            const QPlatformSurfaceEvent *e = static_cast<QPlatformSurfaceEvent*>(event);

            if (e->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated)
                m_frameWindow->create();

            break;
        }
        case QEvent::DynamicPropertyChange: {
            QDynamicPropertyChangeEvent *e = static_cast<QDynamicPropertyChangeEvent*>(event);

            if (e->propertyName() == netWmStates) {
//                m_store->onWindowStateChanged();
            } else if (e->propertyName() == windowRadius) {
                updateWindowRadiusFromProperty();
            } else if (e->propertyName() == borderWidth) {
                updateBorderWidthFromProperty();
            } else if (e->propertyName() == borderColor) {
                updateBorderColorFromProperty();
            } else if (e->propertyName() == shadowRadius) {
                updateShadowRadiusFromProperty();
            } else if (e->propertyName() == shadowOffset) {
                updateShadowOffsetFromProperty();
            } else if (e->propertyName() == shadowColor) {
                updateShadowColorFromProperty();
            } else if (e->propertyName() == clipPath) {
                updateClipPathFromProperty();
            } else if (e->propertyName() == frameMask) {
                updateFrameMaskFromProperty();
            } else if (e->propertyName() == enableSystemResize) {
                updateEnableSystemResizeFromProperty();
            } else if (e->propertyName() == enableSystemMove) {
                updateEnableSystemMoveFromProperty();
            } else if (e->propertyName() == enableBlurWindow) {
                updateEnableBlurWindowFromProperty();
            } else if (e->propertyName() == windowBlurAreas) {
                updateWindowBlurAreasFromProperty();
            } else if (e->propertyName() == windowBlurPaths) {
                updateWindowBlurPathsFromProperty();
            } else if (e->propertyName() == autoInputMaskByClipPath) {
                updateAutoInputMaskByClipPathFromProperty();
            }

            break;
        }
        case QEvent::Resize:
            if (m_isUserSetClipPath) {
                setWindowVaildGeometry(m_clipPath.boundingRect().toRect() & QRect(QPoint(0, 0), static_cast<QResizeEvent*>(event)->size()));
            } else {
                updateClipPathByWindowRadius(static_cast<QResizeEvent*>(event)->size());
            }
            break;
        case QEvent::Leave: {
            const QPoint &pos = Utility::translateCoordinates(QPoint(0, 0), m_nativeWindow->winId(),
                                                              DPlatformIntegration::instance()->defaultConnection()->rootWindow());
            const QPoint &cursor_pos = qApp->primaryScreen()->handle()->cursor()->pos();

            return m_clipPath.contains(QPointF(cursor_pos - pos) / m_nativeWindow->window()->devicePixelRatio());
        }
        default: break;
        }
    }

    return false;
}

void DPlatformWindowHelper::updateClipPathByWindowRadius(const QSize &windowSize)
{
    if (!m_isUserSetClipPath) {
        setWindowVaildGeometry(QRect(QPoint(0, 0), windowSize));

        int window_radius = getWindowRadius();

        QPainterPath path;

        path.addRoundedRect(m_windowVaildGeometry, window_radius, window_radius);

        setClipPath(path);
    }
}

void DPlatformWindowHelper::setClipPath(const QPainterPath &path)
{
    if (m_clipPath == path)
        return;

    m_clipPath = path;

    if (m_isUserSetClipPath) {
        setWindowVaildGeometry(m_clipPath.boundingRect().toRect() & QRect(QPoint(0, 0), m_nativeWindow->window()->size()));
    }

    const QPainterPath &real_path = m_clipPath * m_nativeWindow->window()->devicePixelRatio();
    QPainterPathStroker stroker;

    stroker.setJoinStyle(Qt::MiterJoin);
    stroker.setWidth(1);

    Utility::setShapePath(m_nativeWindow->QNativeWindow::winId(),
                          stroker.createStroke(real_path).united(real_path),
                          false);

    updateWindowBlurAreasForWM();
    updateContentPathForFrameWindow();
}

void DPlatformWindowHelper::setWindowVaildGeometry(const QRect &geometry)
{
    if (geometry == m_windowVaildGeometry)
        return;

    m_windowVaildGeometry = geometry;

    updateWindowBlurAreasForWM();
}

bool DPlatformWindowHelper::updateWindowBlurAreasForWM()
{
    qreal device_pixel_ratio = m_nativeWindow->window()->devicePixelRatio();
    const QRect &windowValidRect = m_windowVaildGeometry * device_pixel_ratio;

    if (windowValidRect.isEmpty() || !m_nativeWindow->window()->isVisible())
        return false;

    quint32 top_level_w = Utility::getNativeTopLevelWindow(m_frameWindow->winId());
    QPoint offset = m_frameWindow->contentOffsetHint() * device_pixel_ratio;

    if (top_level_w != m_frameWindow->winId()) {
        offset += Utility::translateCoordinates(QPoint(0, 0), m_frameWindow->winId(), top_level_w);
    }

    QVector<Utility::BlurArea> newAreas;

    if (m_enableBlurWindow) {
        if (m_isUserSetClipPath) {
            QList<QPainterPath> list;

            list << (m_clipPath * device_pixel_ratio).translated(offset);

            return Utility::blurWindowBackgroundByPaths(top_level_w, list);
        }

        Utility::BlurArea area;

        area.x = windowValidRect.x() + offset.x();
        area.y = windowValidRect.y() + offset.y();
        area.width = windowValidRect.width();
        area.height = windowValidRect.height();
        area.xRadius = getWindowRadius() * device_pixel_ratio;
        area.yRaduis = getWindowRadius() * device_pixel_ratio;

        newAreas.append(std::move(area));

        return Utility::blurWindowBackground(top_level_w, newAreas);
    }

    if (!m_isUserSetClipPath && m_windowRadius <=0 && m_blurPathList.isEmpty()) {
        if (m_blurAreaList.isEmpty())
            return true;

        newAreas.reserve(m_blurAreaList.size());

        foreach (Utility::BlurArea area, m_blurAreaList) {
            area *= device_pixel_ratio;

            if (area.x < 0) {
                area.width += area.x;
                area.x = 0;
            }

            if (area.y < 0) {
                area.height += area.y;
                area.y = 0;
            }

            area.width = qMin(area.x + area.width, windowValidRect.right() + 1) - area.x;
            area.height = qMin(area.y + area.height, windowValidRect.bottom() + 1) - area.y;
            area.x += offset.x();
            area.y += offset.y();

            newAreas.append(std::move(area));
        }

        return Utility::blurWindowBackground(top_level_w, newAreas);
    }

    QList<QPainterPath> newPathList;

    newPathList.reserve(newAreas.size());

    QPainterPath window_vaild_path = (m_clipPath * device_pixel_ratio).translated(offset);

    foreach (Utility::BlurArea area, m_blurAreaList) {
        QPainterPath path;

        area *= device_pixel_ratio;
        path.addRoundedRect(area.x + offset.x(), area.y + offset.y(), area.width, area.height, area.xRadius, area.yRaduis);
        path = path.intersected(window_vaild_path);

        if (!path.isEmpty())
            newPathList << path;
    }

    if (!m_blurPathList.isEmpty()) {
        newPathList.reserve(newPathList.size() + m_blurPathList.size());

        foreach (const QPainterPath &path, m_blurPathList) {
            newPathList << (path * device_pixel_ratio).translated(offset).intersected(window_vaild_path);
        }
    }

    if (newPathList.isEmpty())
        return true;

    return Utility::blurWindowBackgroundByPaths(top_level_w, newPathList);
}

void DPlatformWindowHelper::updateSizeHints()
{
    const QMargins &content_margins = m_frameWindow->contentMarginsHint();
    const QSize extra_size(content_margins.left() + content_margins.right(),
                           content_margins.top() + content_margins.bottom());

    qt_window_private(m_frameWindow)->minimumSize = m_nativeWindow->window()->minimumSize() + extra_size;
    qt_window_private(m_frameWindow)->maximumSize = m_nativeWindow->window()->maximumSize() + extra_size;
    qt_window_private(m_frameWindow)->baseSize = m_nativeWindow->window()->baseSize() + extra_size;
    qt_window_private(m_frameWindow)->sizeIncrement = m_nativeWindow->window()->sizeIncrement();

    m_frameWindow->handle()->propagateSizeHints();
}

void DPlatformWindowHelper::updateContentPathForFrameWindow()
{
    if (m_isUserSetClipPath) {
        m_frameWindow->setContentPath(m_clipPath);
    } else {
        m_frameWindow->setContentRoundedRect(m_windowVaildGeometry, getWindowRadius());
    }
}

int DPlatformWindowHelper::getWindowRadius() const
{
    return (m_isUserSetWindowRadius || DWMSupport::instance()->hasComposite()) ? m_windowRadius : 0;
}

int DPlatformWindowHelper::getShadowRadius() const
{
    return /*DWMSupport::instance()->hasComposite() ?*/ m_shadowRadius /*: 0*/;
}

static QColor colorBlend(const QColor &color1, const QColor &color2)
{
    QColor c2 = color2.toRgb();

    if (c2.alpha() >= 255)
        return c2;

    QColor c1 = color1.toRgb();
    qreal c1_weight = 1 - c2.alphaF();

    int r = c1_weight * c1.red() + c2.alphaF() * c2.red();
    int g = c1_weight * c1.green() + c2.alphaF() * c2.green();
    int b = c1_weight * c1.blue() + c2.alphaF() * c2.blue();

    return QColor(r, g, b);
}

QColor DPlatformWindowHelper::getBorderColor() const
{
    return DWMSupport::instance()->hasComposite() ? m_borderColor : colorBlend(QColor("#e0e0e0"), m_borderColor);
}

void DPlatformWindowHelper::updateWindowRadiusFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(windowRadius);

    if (!v.isValid()) {
        m_nativeWindow->window()->setProperty(windowRadius, m_windowRadius);

        return;
    }

    bool ok;
    int radius = v.toInt(&ok);

    if (ok && radius != m_windowRadius) {
        m_windowRadius = radius;
        m_isUserSetWindowRadius = true;
        m_isUserSetClipPath = false;

        updateClipPathByWindowRadius(m_nativeWindow->window()->size());
    }
}

void DPlatformWindowHelper::updateBorderWidthFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(borderWidth);

    if (!v.isValid()) {
        m_nativeWindow->window()->setProperty(borderWidth, m_borderWidth);

        return;
    }

    bool ok;
    int width = v.toInt(&ok);

    if (ok && width != m_borderWidth) {
        m_borderWidth = width;
        m_frameWindow->setBorderWidth(width);
    }
}

void DPlatformWindowHelper::updateBorderColorFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(borderColor);

    if (!v.isValid()) {
        m_nativeWindow->window()->setProperty(borderColor, m_borderColor);

        return;
    }

    const QColor &color = qvariant_cast<QColor>(v);

    if (color.isValid() && m_borderColor != color) {
        m_borderColor = color;
        m_frameWindow->setBorderColor(getBorderColor());
    }
}

void DPlatformWindowHelper::updateShadowRadiusFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(shadowRadius);

    if (!v.isValid()) {
        m_nativeWindow->window()->setProperty(shadowRadius, m_shadowRadius);

        return;
    }

    bool ok;
    int radius = qMax(v.toInt(&ok), 0);

    if (ok && radius != m_shadowRadius) {
        m_shadowRadius = radius;

        if (DWMSupport::instance()->hasComposite())
            m_frameWindow->setShadowRadius(radius);
    }
}

void DPlatformWindowHelper::updateShadowOffsetFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(shadowOffset);

    if (!v.isValid()) {
        m_nativeWindow->window()->setProperty(shadowOffset, m_shadowOffset);

        return;
    }

    const QPoint &offset = v.toPoint();

    if (offset != m_shadowOffset) {
        m_shadowOffset = offset;
        m_frameWindow->setShadowOffset(offset);
    }
}

void DPlatformWindowHelper::updateShadowColorFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(shadowColor);

    if (!v.isValid()) {
        m_nativeWindow->window()->setProperty(shadowColor, m_shadowColor);

        return;
    }

    const QColor &color = qvariant_cast<QColor>(v);

    if (color.isValid() && m_shadowColor != color) {
        m_shadowColor = color;
        m_frameWindow->setShadowColor(color);
    }
}

void DPlatformWindowHelper::updateEnableSystemResizeFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(enableSystemResize);

    if (!v.isValid()) {
        m_nativeWindow->window()->setProperty(enableSystemResize, m_enableSystemResize);

        return;
    }

    if (m_enableSystemResize == v.toBool())
        return;

    m_enableSystemResize = v.toBool();
    m_frameWindow->setEnableSystemResize(m_enableSystemResize);
}

void DPlatformWindowHelper::updateEnableSystemMoveFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(enableSystemMove);

    if (!v.isValid()) {
        m_nativeWindow->window()->setProperty(enableSystemMove, m_enableSystemMove);

        return;
    }

    m_enableSystemMove = v.toBool();
    m_frameWindow->setEnableSystemMove(m_enableSystemMove);
}

void DPlatformWindowHelper::updateEnableBlurWindowFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(enableBlurWindow);

    if (!v.isValid()) {
        m_nativeWindow->window()->setProperty(enableBlurWindow, m_enableBlurWindow);

        return;
    }

    if (m_enableBlurWindow != v.toBool()) {
        m_enableBlurWindow = v.toBool();

        if (m_enableBlurWindow) {
            QObject::connect(DWMSupport::instance(), &DWMSupport::windowManagerChanged,
                             this, &DPlatformWindowHelper::updateWindowBlurAreasForWM);
        } else {
            QObject::disconnect(DWMSupport::instance(), &DWMSupport::windowManagerChanged,
                                this, &DPlatformWindowHelper::updateWindowBlurAreasForWM);
        }

        updateWindowBlurAreasForWM();
    }
}

void DPlatformWindowHelper::updateWindowBlurAreasFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(windowBlurAreas);
    const QVector<quint32> &tmpV = qvariant_cast<QVector<quint32>>(v);
    const QVector<Utility::BlurArea> &a = *(reinterpret_cast<const QVector<Utility::BlurArea>*>(&tmpV));

    if (a.isEmpty() && m_blurAreaList.isEmpty())
        return;

    m_blurAreaList = a;

    updateWindowBlurAreasForWM();
}

void DPlatformWindowHelper::updateWindowBlurPathsFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(windowBlurPaths);
    const QList<QPainterPath> paths = qvariant_cast<QList<QPainterPath>>(v);

    if (paths.isEmpty() && m_blurPathList.isEmpty())
        return;

    m_blurPathList = paths;

    updateWindowBlurAreasForWM();
}

void DPlatformWindowHelper::updateAutoInputMaskByClipPathFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(autoInputMaskByClipPath);

    if (!v.isValid()) {
        m_nativeWindow->window()->setProperty(autoInputMaskByClipPath, m_autoInputMaskByClipPath);

        return;
    }

    if (m_autoInputMaskByClipPath != v.toBool()) {
        m_autoInputMaskByClipPath = v.toBool();
    }

    m_frameWindow->m_enableAutoInputMaskByContentPath = m_autoInputMaskByClipPath;
}

void DPlatformWindowHelper::onFrameWindowContentMarginsHintChanged(const QMargins &oldMargins)
{
    updateWindowBlurAreasForWM();
    updateSizeHints();

    // update the content window gemetry
    QRect rect = m_nativeWindow->QNativeWindow::geometry();
    rect.moveTopLeft(m_frameWindow->contentOffsetHint() * m_nativeWindow->window()->devicePixelRatio());
    m_nativeWindow->window()->setProperty(::frameMargins, QVariant::fromValue(m_frameWindow->contentMarginsHint()));
    m_frameWindowSize = (m_frameWindow->geometry() + m_frameWindow->contentMarginsHint() - oldMargins).size();
    m_frameWindow->setGeometry(m_frameWindow->geometry() + m_frameWindow->contentMarginsHint() - oldMargins);
    m_nativeWindow->QNativeWindow::setGeometry(rect);
    // hide in DFrameWindow::updateContentMarginsHint
    m_nativeWindow->QNativeWindow::setVisible(true);
}

void DPlatformWindowHelper::onWMHasCompositeChanged()
{
    const QSize &window_size = m_nativeWindow->window()->size();

    updateClipPathByWindowRadius(window_size);

//    m_frameWindow->setShadowRaduis(getShadowRadius());
    Utility::setShapePath(m_nativeWindow->QNativeWindow::winId(),
                          m_clipPath * m_nativeWindow->window()->devicePixelRatio(),
                          false);

    m_frameWindow->updateMask();
    m_frameWindow->setBorderColor(getBorderColor());

    if (m_nativeWindow->window()->inherits("QWidgetWindow")) {
        QEvent event(QEvent::UpdateRequest);
        qApp->sendEvent(m_nativeWindow->window(), &event);
    } else {
        QMetaObject::invokeMethod(m_nativeWindow->window(), "update");
    }
}

void DPlatformWindowHelper::updateClipPathFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(clipPath);

    if (!v.isValid()) {
        return;
    }

    QPainterPath path;

    path = qvariant_cast<QPainterPath>(v);

    if (!m_isUserSetClipPath && path.isEmpty())
        return;

    m_isUserSetClipPath = !path.isEmpty();

    if (m_isUserSetClipPath)
        setClipPath(path);
    else
        updateClipPathByWindowRadius(m_nativeWindow->window()->size());
}

void DPlatformWindowHelper::updateFrameMaskFromProperty()
{
    const QVariant &v = m_nativeWindow->window()->property(frameMask);

    if (!v.isValid()) {
        return;
    }

    QRegion region = qvariant_cast<QRegion>(v);

    m_frameWindow->setMask(region * m_frameWindow->devicePixelRatio());
    m_isUserSetFrameMask = !region.isEmpty();
    m_frameWindow->m_enableAutoFrameMask = !m_isUserSetFrameMask;
}

DPP_END_NAMESPACE
