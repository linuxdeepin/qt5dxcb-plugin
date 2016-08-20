#ifndef TESTWINDOW_H
#define TESTWINDOW_H

#include "qxcbwindow.h"

class XcbWindowHook
{
public:
    XcbWindowHook(QXcbWindow *window);

    ~XcbWindowHook();

    QXcbWindow *window() const
    { return static_cast<QXcbWindow*>(reinterpret_cast<QPlatformWindow*>(const_cast<XcbWindowHook*>(this)));}

    XcbWindowHook *me() const;

    void setGeometry(const QRect &rect);
    QRect geometry() const;

    QMargins frameMargins() const;

    void setParent(const QPlatformWindow *window);

    void setWindowTitle(const QString &title);
    void setWindowIcon(const QIcon &icon);

//    QPoint mapToGlobal(const QPoint &pos) const;
//    QPoint mapFromGlobal(const QPoint &pos) const;

    void setMask(const QRegion &region);

//    bool startSystemResize(const QPoint &pos, Qt::Corner corner);

    void propagateSizeHints();

    static XcbWindowHook *getHookByWindow(const QPlatformWindow *window);

private:
    QMargins windowMargins;
    static QHash<const QPlatformWindow*, XcbWindowHook*> mapped;

    friend class DXcbBackingStore;
};

Q_DECLARE_METATYPE(QPainterPath)

#endif // TESTWINDOW_H
