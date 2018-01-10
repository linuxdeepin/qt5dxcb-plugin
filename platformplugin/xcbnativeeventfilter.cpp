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

#include "xcbnativeeventfilter.h"
#include "utility.h"
#include "dplatformwindowhelper.h"
#include "dframewindow.h"

#define private public
#include "qxcbconnection.h"
#include "qxcbclipboard.h"
#undef private

#include "dplatformintegration.h"
#include "dxcbwmsupport.h"

#include <xcb/xfixes.h>
#include <xcb/damage.h>
#include <X11/extensions/XI2proto.h>
#include <X11/extensions/XInput2.h>

#include <cmath>

DPP_BEGIN_NAMESPACE

XcbNativeEventFilter::XcbNativeEventFilter(QXcbConnection *connection)
    : m_connection(connection)
{
    // init damage first event value
    xcb_prefetch_extension_data(connection->xcb_connection(), &xcb_damage_id);
    const auto* reply = xcb_get_extension_data(connection->xcb_connection(), &xcb_damage_id);

    if (reply->present) {
      m_damageFirstEvent = reply->first_event;
      xcb_damage_query_version_unchecked(connection->xcb_connection(), XCB_DAMAGE_MAJOR_VERSION, XCB_DAMAGE_MINOR_VERSION);
    } else {
        m_damageFirstEvent = 0;
    }
}

QClipboard::Mode XcbNativeEventFilter::clipboardModeForAtom(xcb_atom_t a) const
{
    if (a == XCB_ATOM_PRIMARY)
        return QClipboard::Selection;
    if (a == m_connection->atom(QXcbAtom::CLIPBOARD))
        return QClipboard::Clipboard;
    // not supported enum value, used to detect errors
    return QClipboard::FindBuffer;
}

typedef struct qt_xcb_ge_event_t {
    uint8_t  response_type;
    uint8_t  extension;
    uint16_t sequence;
    uint32_t length;
    uint16_t event_type;
} qt_xcb_ge_event_t;

static inline bool isXIEvent(xcb_generic_event_t *event, int opCode)
{
    qt_xcb_ge_event_t *e = (qt_xcb_ge_event_t *)event;
    return e->extension == opCode;
}

bool XcbNativeEventFilter::nativeEventFilter(const QByteArray &eventType, void *message, long *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)

    xcb_generic_event_t *event = reinterpret_cast<xcb_generic_event_t*>(message);
    uint response_type = event->response_type & ~0x80;

    if (response_type == m_connection->xfixes_first_event + XCB_XFIXES_SELECTION_NOTIFY) {
        xcb_xfixes_selection_notify_event_t *xsn = (xcb_xfixes_selection_notify_event_t *)event;

        if (xsn->selection == DPlatformIntegration::xcbConnection()->atom(QXcbAtom::_NET_WM_CM_S0)) {
            DXcbWMSupport::instance()->updateHasComposite();
        }

        QClipboard::Mode mode = clipboardModeForAtom(xsn->selection);
        if (mode > QClipboard::Selection)
            return false;

        // here we care only about the xfixes events that come from non Qt processes
        if (xsn->owner == XCB_NONE && xsn->subtype == XCB_XFIXES_SELECTION_EVENT_SET_SELECTION_OWNER) {
            QXcbClipboard *xcbClipboard = m_connection->m_clipboard;
            xcbClipboard->emitChanged(mode);
        }
    } else if (response_type == m_damageFirstEvent + XCB_DAMAGE_NOTIFY) {
        xcb_damage_notify_event_t *ev = (xcb_damage_notify_event_t*)event;

        QXcbWindow *window = m_connection->platformWindowFromId(ev->drawable);

        if (Q_LIKELY(window)) {
            DPlatformWindowHelper *helper = DPlatformWindowHelper::mapped.value(window);

            if (Q_LIKELY(helper)) {
                helper->m_frameWindow->updateFromContents(ev);
            }
        }
    } else {
        switch (response_type) {
        case XCB_PROPERTY_NOTIFY: {
            xcb_property_notify_event_t *pn = (xcb_property_notify_event_t *)event;

            if (pn->atom == DPlatformIntegration::xcbConnection()->atom(QXcbAtom::_MOTIF_WM_HINTS)) {
                emit DXcbWMSupport::instance()->windowMotifWMHintsChanged(pn->window);
            } else {
                if (pn->window != DPlatformIntegration::instance()->defaultConnection()->rootWindow()) {
                    return false;
                }

                if (pn->atom == DPlatformIntegration::instance()->defaultConnection()->atom(QXcbAtom::_NET_SUPPORTED)) {
                    DXcbWMSupport::instance()->updateNetWMAtoms();
                } else if (pn->atom == DPlatformIntegration::instance()->defaultConnection()->atom(QXcbAtom::_NET_SUPPORTING_WM_CHECK)) {
                    DXcbWMSupport::instance()->updateWMName();
                } else if (pn->atom == DXcbWMSupport::instance()->_kde_net_wm_blur_rehind_region_atom) {
                    DXcbWMSupport::instance()->updateRootWindowProperties();
                } else if (pn->atom == Utility::internAtom("_NET_CLIENT_LIST_STACKING")) {
                    emit DXcbWMSupport::instance()->windowListChanged();
                }
            }
            break;
        }
            // 修复Qt程序对触摸板的自然滚动开关后不能实时生效
            // 由于在收到xi的DeviceChanged事件后，Qt更新ScrollingDevice时没有更新verticalIncrement字段
            // 导致那些使用increment的正负值控制自然滚动开关的设备对Qt程序无法实时生效
            // 有些电脑上触摸板没有此问题，是因为他的系统环境中没有安装xserver-xorg-input-synaptics
#ifdef XCB_USE_XINPUT21
        case XCB_GE_GENERIC: {
            QXcbConnection *xcb_connect = DPlatformIntegration::xcbConnection();

            if (xcb_connect->m_xi2Enabled && isXIEvent(event, xcb_connect->m_xiOpCode)) {
                xXIGenericDeviceEvent *xiEvent = reinterpret_cast<xXIGenericDeviceEvent *>(event);

                if (xiEvent->evtype != XI_DeviceChanged) {
                    return false;
                }

                xXIDeviceChangedEvent *xiDCEvent = reinterpret_cast<xXIDeviceChangedEvent *>(xiEvent);
                QHash<int, QXcbConnection::ScrollingDevice>::iterator device = xcb_connect->m_scrollingDevices.find(xiDCEvent->sourceid);

                int nrDevices = 0;
                XIDeviceInfo* xiDeviceInfo = XIQueryDevice(static_cast<Display *>(xcb_connect->xlib_display()), xiDCEvent->sourceid, &nrDevices);

                if (nrDevices <= 0) {
                    return false;
                }


                for (int c = 0; c < xiDeviceInfo->num_classes; ++c) {
                    if (xiDeviceInfo->classes[c]->type == XIScrollClass) {
                        XIScrollClassInfo *sci = reinterpret_cast<XIScrollClassInfo *>(xiDeviceInfo->classes[c]);

                        if (sci->scroll_type == XIScrollTypeVertical) {
                            device->legacyOrientations = device->orientations;
                            device->orientations |= Qt::Vertical;
                            device->verticalIndex = sci->number;
                            device->verticalIncrement = std::signbit(sci->increment)
                                    ? -std::abs(device->verticalIncrement)
                                    : std::abs(device->verticalIncrement);
                        }
                        else if (sci->scroll_type == XIScrollTypeHorizontal) {
                            device->legacyOrientations = device->orientations;
                            device->orientations |= Qt::Horizontal;
                            device->horizontalIndex = sci->number;
                            device->horizontalIncrement = std::signbit(sci->increment)
                                    ? -std::abs(device->horizontalIncrement)
                                    : std::abs(device->horizontalIncrement);
                        }
                    }
                }

                XIFreeDeviceInfo(xiDeviceInfo);
            }
            break;
        }
#endif
        default: break;
        }
    }

    return false;
}

DPP_END_NAMESPACE
