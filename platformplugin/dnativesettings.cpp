/*
 * Copyright (C) 2017 ~ 2019 Deepin Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
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
#include "dnativesettings.h"
#ifdef Q_OS_LINUX
#include "dxcbxsettings.h"
#endif
#include "dplatformintegration.h"

#include <QDebug>
#include <QMetaProperty>
#include <QMetaMethod>

#define VALID_PROPERTIES "validProperties"

DPP_BEGIN_NAMESPACE

QHash<QObject*, DNativeSettings*> DNativeSettings::mapped;
/*
 * 通过覆盖QObject的qt_metacall虚函数，检测base object中自定义的属性列表，将xwindow对应的设置和object对象中的属性绑定到一起使用
 * 将对象通过property/setProperty调用对属性的读写操作转为对xsetting的属性设 置
 */
DNativeSettings::DNativeSettings(QObject *base, quint32 settingsWindow)
    : m_base(base)
    , m_objectBuilder(base->metaObject())
{
    if (mapped.value(base)) {
        qCritical() << "DNativeSettings: Native settings are already initialized for object:" << base;
        std::abort();
    }

    mapped[base] = this;

    QByteArray settings_property;

    {
        // 获取base对象是否指定了native settings的域
        // 默认情况下，native settings的值保存在窗口的_XSETTINGS_SETTINGS属性上
        // 指定域后，会将native settings的值保存到指定的窗口属性。
        // 将域的值转换成窗口属性时，会把 "/" 替换为 "_"，如域："/xxx/xxx" 转成窗口属性为："_xxx_xxx"
        // 且所有字母转换为大写
        int index = base->metaObject()->indexOfClassInfo("Domain");

        if (index >= 0) {
            settings_property = QByteArray(base->metaObject()->classInfo(index).value());
            settings_property = settings_property.toUpper();
            settings_property.replace('/', '_');
        }
    }

    // 当指定了窗口或窗口属性时，应当创建一个新的native settings
    if (settingsWindow || !settings_property.isEmpty()) {
        m_settings = new NativeSettings(settingsWindow, settings_property);
    } else {
        m_settings = DPlatformIntegration::instance()->xSettings();
    }

    if (m_settings->initialized()) {
        init();
    }
}

DNativeSettings::~DNativeSettings()
{
    if (m_settings != DPlatformIntegration::instance()->xSettings(true)) {
        delete m_settings;
    }

    mapped.remove(m_base);

    if (m_metaObject) {
        free(m_metaObject);
    }
}

bool DNativeSettings::isValid() const
{
    return m_settings->initialized();
}

void DNativeSettings::init()
{
    m_firstProperty = m_base->metaObject()->propertyOffset();
    m_propertyCount = m_base->metaObject()->propertyCount() - m_base->metaObject()->superClass()->propertyCount();
    // 用于记录属性是否有效的属性, 属性类型为64位整数，最多可用于记录64个属性的状态
    m_flagPropertyIndex = m_base->metaObject()->indexOfProperty(VALID_PROPERTIES);
    qint64 validProperties = 0;

    QMetaObjectBuilder &ob = m_objectBuilder;
    ob.setFlags(ob.flags() | QMetaObjectBuilder::DynamicMetaObject);

    // 先删除所有的属性，等待重构
    while (ob.propertyCount() > 0) {
        ob.removeProperty(0);
    }

    QVector<int> propertySignalIndex;
    propertySignalIndex.reserve(m_propertyCount);

    for (int i = 0; i < m_propertyCount; ++i) {
        int index = i + m_firstProperty;

        const QMetaProperty &mp = m_base->metaObject()->property(index);

        if (mp.hasNotifySignal()) {
            propertySignalIndex << mp.notifySignalIndex();
        }

        // 跳过特殊属性
        if (index == m_flagPropertyIndex) {
            ob.addProperty(mp);
            continue;
        }

        if (m_settings->setting(mp.name()).isValid()) {
            validProperties |= (1 << i);
        }

        QMetaPropertyBuilder op;

        switch (mp.type()) {
        case QMetaType::QByteArray:
        case QMetaType::QString:
        case QMetaType::QColor:
        case QMetaType::Int:
        case QMetaType::Double:
        case QMetaType::Bool:
            op = ob.addProperty(mp);
            break;
        default:
            // 重设属性的类型，只支持Int double color string bytearray
            op = ob.addProperty(mp.name(), "QByteArray", mp.notifySignalIndex());
            break;
        }

        if (op.isWritable()) {
            // 声明支持属性reset
            op.setResettable(true);
        }
    }

    {
        // 通过class info确定是否应该关联对象的信号
        int index = m_base->metaObject()->indexOfClassInfo("SignalType");

        if (index >= 0) {
            const QByteArray signals_value(m_base->metaObject()->classInfo(index).value());

            // 如果base对象声明为信号的生产者，则应该将其产生的信号转发到native settings
            if (signals_value == "producer") {
                // 创建一个槽用于接收所有信号
                m_relaySlotIndex = ob.addMethod("relaySlot(QByteArray,qint32,qint32)").index() + m_base->metaObject()->methodOffset();
            }
        }
    }

    // 将属性状态设置给对象
    m_base->setProperty(VALID_PROPERTIES, validProperties);
    m_propertySignalIndex = m_base->metaObject()->indexOfMethod(QMetaObject::normalizedSignature("propertyChanged(const QByteArray&, const QVariant&)"));
    // 监听native setting变化
    m_settings->registerCallback(reinterpret_cast<NativeSettings::PropertyChangeFunc>(onPropertyChanged), this);
    // 监听信号. 如果base对象声明了要转发其信号，则此对象不应该关心来自于native settings的信号
    // 即信号的生产者和消费者只能选其一
    if (!isRelaySignal()) {
        m_settings->registerSignalCallback(reinterpret_cast<NativeSettings::SignalFunc>(onSignal), this);
    }
    // 支持在base对象中直接使用property/setProperty读写native属性
    QObjectPrivate *op = QObjectPrivate::get(m_base);
    op->metaObject = this;
    m_metaObject = ob.toMetaObject();
    *static_cast<QMetaObject *>(this) = *m_metaObject;

    if (isRelaySignal()) {
        // 把 static_metacall 置为nullptr，迫使对base对象调用QMetaObject::invodeMethod时使用DNativeSettings::metaCall
        d.static_metacall = nullptr;
        // 链接 base 对象的所有信号
        int first_method = methodOffset();
        int method_count = methodCount();

        for (int i = 0; i < method_count; ++i) {
            int index = i + first_method;

            // 排除属性对应的信号
            if (propertySignalIndex.contains(index)) {
                continue;
            }

            QMetaMethod method = this->method(index);

            if (method.methodType() != QMetaMethod::Signal) {
                continue;
            }

            QMetaObject::connect(m_base, index, m_base, m_relaySlotIndex, Qt::DirectConnection);
        }
    }
}

int DNativeSettings::createProperty(const char *name, const char *)
{
    // 清理旧数据
    free(m_metaObject);

    // 添加新属性
    auto property = m_objectBuilder.addProperty(name, "QVariant");
    property.setReadable(true);
    property.setWritable(true);
    property.setResettable(true);
    m_metaObject = m_objectBuilder.toMetaObject();
    *static_cast<QMetaObject *>(this) = *m_metaObject;

    return m_firstProperty + property.index();
}

void DNativeSettings::onPropertyChanged(void *screen, const QByteArray &name, const QVariant &property, DNativeSettings *handle)
{
    Q_UNUSED(screen)

    if (handle->m_propertySignalIndex >= 0) {
        handle->method(handle->m_propertySignalIndex).invoke(handle->m_base, Q_ARG(QByteArray, name), Q_ARG(QVariant, property));
    }

    // 不要直接调用自己的indexOfProperty函数，属性不存在时会导致调用createProperty函数
    int property_index = handle->m_objectBuilder.indexOfProperty(name.constData());

    if (Q_UNLIKELY(property_index < 0)) {
        return;
    }

    if (handle->m_flagPropertyIndex >= 0) {
        bool ok = false;
        qint64 flags = handle->m_base->property(VALID_PROPERTIES).toLongLong(&ok);
        // 更新有效属性的标志位
        if (ok) {
            qint64 flag = (1 << property_index);
            flags = property.isValid() ? flags | flag : flags & ~flag;
            handle->m_base->setProperty(VALID_PROPERTIES, flags);
        }
    }

    const QMetaProperty &p = handle->property(handle->m_firstProperty + property_index);

    if (p.hasNotifySignal()) {
        // 通知属性改变
        p.notifySignal().invoke(handle->m_base);
    }
}

// 处理native settings发过来的信号
void DNativeSettings::onSignal(void *screen, const QByteArray &signal, qint32 data1, qint32 data2, DNativeSettings *handle)
{
    Q_UNUSED(screen)

    // 根据不同的参数寻找对应的信号
    static QByteArrayList signal_suffixs {
        QByteArrayLiteral("()"),
        QByteArrayLiteral("(qint32)"),
        QByteArrayLiteral("(qint32,qint32)")
    };

    int signal_index = -1;

    for (const QByteArray &suffix : signal_suffixs) {
        signal_index = handle->indexOfMethod(signal + suffix);

        if (signal_index >= 0)
            break;
    }

    QMetaMethod signal_method = handle->method(signal_index);
    // 调用base对象对应的信号
    signal_method.invoke(handle->m_base, Qt::DirectConnection, Q_ARG(qint32, data1), Q_ARG(qint32, data2));
}

int DNativeSettings::metaCall(QMetaObject::Call _c, int _id, void ** _a)
{
    enum CallFlag {
        ReadProperty = 1 << QMetaObject::ReadProperty,
        WriteProperty = 1 << QMetaObject::WriteProperty,
        ResetProperty = 1 << QMetaObject::ResetProperty,
        AllCall = ReadProperty | WriteProperty | ResetProperty
    };

    if (AllCall & (1 << _c)) {
        const QMetaProperty &p = property(_id);
        const int index = p.propertyIndex();
        // 对于本地属性，此处应该从m_settings中读写
        if (Q_LIKELY(index != m_flagPropertyIndex && index >= m_firstProperty)) {
            switch (_c) {
            case QMetaObject::ReadProperty:
                *reinterpret_cast<QVariant*>(_a[1]) = m_settings->setting(p.name());
                _a[0] = reinterpret_cast<QVariant*>(_a[1])->data();
                break;
            case QMetaObject::WriteProperty:
                m_settings->setSetting(p.name(), *reinterpret_cast<QVariant*>(_a[1]));
                break;
            case QMetaObject::ResetProperty:
                m_settings->setSetting(p.name(), QVariant());
                break;
            default:
                break;
            }

            return -1;
        }
    }

    do {
        if (!isRelaySignal())
            break;

        if (Q_LIKELY(_c != QMetaObject::InvokeMetaMethod || _id != m_relaySlotIndex)) {
            break;
        }

        int signal = m_base->senderSignalIndex();
        QByteArray signal_name;
        qint32 data1 = 0, data2 = 0;

        // 不是通过信号触发的槽调用，可能是使用QMetaObject::invoke
        if (signal < 0) {
            signal_name = *reinterpret_cast<QByteArray*>(_a[1]);
            data1 = *reinterpret_cast<qint32*>(_a[2]);
            data2 = *reinterpret_cast<qint32*>(_a[3]);
        } else {
            const auto &signal_method = method(signal);
            signal_name = signal_method.name();

            // 0为return type, 因此参数值下标从1开始
            if (signal_method.parameterCount() > 0) {
                QVariant arg(signal_method.parameterType(0), _a[1]);
                // 获取参数1，获取参数2
                data1 = arg.toInt();
            }

            if (signal_method.parameterCount() > 1) {
                QVariant arg(signal_method.parameterType(1), _a[2]);
                data2 = arg.toInt();
            }
        }

        m_settings->emitSignal(signal_name, data1, data2);

        return -1;
    } while (false);

    return m_base->qt_metacall(_c, _id, _a);
}

bool DNativeSettings::isRelaySignal() const
{
    return m_relaySlotIndex > 0;
}

DPP_END_NAMESPACE
