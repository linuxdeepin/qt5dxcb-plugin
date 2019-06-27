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

#ifndef DPLATFORMNATIVEINTERFACE_H
#define DPLATFORMNATIVEINTERFACE_H

#include <QtGlobal>

#include "global.h"

QT_BEGIN_NAMESPACE
class QPlatformNativeInterface;
QT_END_NAMESPACE

DPP_BEGIN_NAMESPACE

class DPlatformNativeInterfaceHook
{
public:
    static QFunctionPointer platformFunction(QPlatformNativeInterface *interface, const QByteArray &function);
    static thread_local QHash<QByteArray, QFunctionPointer> functionCache;
};

DPP_END_NAMESPACE

#endif // DPLATFORMNATIVEINTERFACE_H
