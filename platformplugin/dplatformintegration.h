/*
 * Copyright (C) 2017 ~ 2018 Deepin Technology Co., Ltd.
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

#ifndef GENERICPLUGIN_H
#define GENERICPLUGIN_H

#include "global.h"

#include <QtGlobal>

#ifdef Q_OS_LINUX
#include "qxcbintegration.h"
typedef QXcbIntegration DPlatformIntegrationParent;
#elif defined(Q_OS_WIN)
#include "qwindowsgdiintegration.h"
typedef QWindowsGdiIntegration DPlatformIntegrationParent;
#endif

DPP_BEGIN_NAMESPACE

class DPlatformWindowHook;
class XcbNativeEventFilter;
class DPlatformBackingStoreHelper;
class DPlatformOpenGLContextHelper;
class DXcbXSettings;

class DPlatformIntegration : public DPlatformIntegrationParent
{
public:
    DPlatformIntegration(const QStringList &parameters, int &argc, char **argv);
    ~DPlatformIntegration();

    static void setWindowProperty(QWindow *window, const char *name, const QVariant &value);

    static bool enableDxcb(QWindow *window);
    static bool isEnableDxcb(const QWindow *window);

    static bool setEnableNoTitlebar(QWindow *window, bool enable);
    static bool isEnableNoTitlebar(const QWindow *window);

    // Warning: 调用 buildNativeSettings，会导致object的QMetaObject对象被更改
    // 无法使用QMetaObject::cast，不能使用QObject::findChild等接口查找子类，也不能使用qobject_cast转换对象指针类型
    static bool buildNativeSettings(QObject *object, quint32 settingWindow);
    static void clearNativeSettings(quint32 settingWindow);

    QPlatformWindow *createPlatformWindow(QWindow *window) const Q_DECL_OVERRIDE;
    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const Q_DECL_OVERRIDE;
    QPlatformOpenGLContext *createPlatformOpenGLContext(QOpenGLContext *context) const Q_DECL_OVERRIDE;
    QPaintEngine *createImagePaintEngine(QPaintDevice *paintDevice) const override;

    QStringList themeNames() const Q_DECL_OVERRIDE;
    QVariant styleHint(StyleHint hint) const override;

    void initialize() Q_DECL_OVERRIDE;

#ifdef Q_OS_LINUX

#if QT_VERSION < QT_VERSION_CHECK(5, 5, 0)
    static DPlatformIntegration *instance() { return m_instance; }

private:
    static DPlatformIntegration *m_instance;
#endif

    inline static DPlatformIntegration *instance()
    { return static_cast<DPlatformIntegration*>(DPlatformIntegrationParent::instance());}

    inline static QXcbConnection *xcbConnection()
    { return instance()->defaultConnection();}

    inline XcbNativeEventFilter *eventFilter()
    { return m_eventFilter;}

    bool enableCursorBlink() const;
    DXcbXSettings *xSettings(bool onlyExists = false) const;

private:
    XcbNativeEventFilter *m_eventFilter = Q_NULLPTR;
    DXcbXSettings *m_xsettings = nullptr;
#endif
private:
    // handle the DFrameWindow modal blocked state
    bool isWindowBlockedHandle(QWindow *window, QWindow **blockingWindow);

    DPlatformBackingStoreHelper *m_storeHelper;
    DPlatformOpenGLContextHelper *m_contextHelper;
};

DPP_END_NAMESPACE

#endif // GENERICPLUGIN_H
