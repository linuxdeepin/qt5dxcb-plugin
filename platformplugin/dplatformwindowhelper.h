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

#ifndef DPLATFORMWINDOWHELPER_H
#define DPLATFORMWINDOWHELPER_H

#include <QtGlobal>

#ifdef Q_OS_LINUX
#define private public
#include "qxcbwindow.h"
#include "qxcbclipboard.h"
typedef QXcbWindow QNativeWindow;
#undef private
#elif defined(Q_OS_WIN)
#include "qwindowswindow.h"
typedef QWindowsWindow QNativeWindow;
#endif

#include "global.h"
#include "utility.h"

DPP_BEGIN_NAMESPACE

class DFrameWindow;
class DPlatformWindowHelper : public QObject
{
public:
    explicit DPlatformWindowHelper(QNativeWindow *window);
    ~DPlatformWindowHelper();

    QNativeWindow *window() const
    { return static_cast<QNativeWindow*>(reinterpret_cast<QPlatformWindow*>(const_cast<DPlatformWindowHelper*>(this)));}

    DPlatformWindowHelper *me() const;

    void setGeometry(const QRect &rect);
    QRect geometry() const;
    QRect normalGeometry() const;

    QMargins frameMargins() const;

    void setVisible(bool visible);
    void setWindowFlags(Qt::WindowFlags flags);
    void setWindowState(Qt::WindowState state);

    WId winId() const;
    void setParent(const QPlatformWindow *window);

    void setWindowTitle(const QString &title);
    void setWindowFilePath(const QString &title);
    void setWindowIcon(const QIcon &icon);
    void raise();
    void lower();

    bool isExposed() const;
//    bool isActive() const;
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    bool isEmbedded() const;
#else
    bool isEmbedded(const QPlatformWindow *parentWindow = 0) const;
#endif

    void propagateSizeHints();

    void setOpacity(qreal level);

    void requestActivateWindow();

    bool setKeyboardGrabEnabled(bool grab);
    bool setMouseGrabEnabled(bool grab);

    bool setWindowModified(bool modified);

    bool startSystemResize(const QPoint &pos, Qt::Corner corner);

    void setFrameStrutEventsEnabled(bool enabled);
    bool frameStrutEventsEnabled() const;

    void setAlertState(bool enabled);
    bool isAlertState() const;

private:
    bool eventFilter(QObject *watched, QEvent *event) Q_DECL_OVERRIDE;
    void setNativeWindowGeometry(const QRect &rect);

    void updateClipPathByWindowRadius(const QSize &windowSize);
    void setClipPath(const QPainterPath &path);
    void setWindowVaildGeometry(const QRect &geometry);
    bool updateWindowBlurAreasForWM();
    void updateSizeHints();
    void updateContentPathForFrameWindow();

    int getWindowRadius() const;
    int getShadowRadius() const;
    QColor getBorderColor() const;
    QPainterPath getClipPath() const;

    // update propertys
    void updateClipPathFromProperty();
    void updateFrameMaskFromProperty();
    void updateWindowRadiusFromProperty();
    void updateBorderWidthFromProperty();
    void updateBorderColorFromProperty();
    void updateShadowRadiusFromProperty();
    void updateShadowOffsetFromProperty();
    void updateShadowColorFromProperty();
    void updateEnableSystemResizeFromProperty();
    void updateEnableSystemMoveFromProperty();
    void updateEnableBlurWindowFromProperty();
    void updateWindowBlurAreasFromProperty();
    void updateWindowBlurPathsFromProperty();
    void updateAutoInputMaskByClipPathFromProperty();

    void onFrameWindowContentMarginsHintChanged(const QMargins &old_margins);
    void onWMHasCompositeChanged();

    static QHash<const QPlatformWindow*, DPlatformWindowHelper*> mapped;

    QNativeWindow *m_nativeWindow;
    DFrameWindow *m_frameWindow;
    QSize m_frameWindowSize;

    QRect m_windowVaildGeometry;
    bool overrideSetGeometry = true;

    // propertys
    bool m_isUserSetClipPath = false;
    QPainterPath m_clipPath;

    bool m_isUserSetFrameMask = false;

    int m_windowRadius = 4;
    bool m_isUserSetWindowRadius = false;

    int m_borderWidth = 1;
    QColor m_borderColor = QColor(0, 0, 0, 255 * 0.15);

    int m_shadowRadius = 60;
    QPoint m_shadowOffset = QPoint(0, 16);
    QColor m_shadowColor = QColor(0, 0, 0, 255 * 0.6);

    bool m_enableSystemResize = true;
    bool m_enableSystemMove = true;
    bool m_enableBlurWindow = false;
    bool m_autoInputMaskByClipPath = true;
    bool m_enableShadow = true;

    QVector<Utility::BlurArea> m_blurAreaList;
    QList<QPainterPath> m_blurPathList;

    friend class DPlatformBackingStoreHelper;
    friend class DPlatformOpenGLContextHelper;
    friend class DPlatformIntegration;
    friend QWindow *topvelWindow(QWindow *);
};

DPP_END_NAMESPACE

#endif // DPLATFORMWINDOWHELPER_H
