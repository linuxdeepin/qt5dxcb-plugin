#ifndef UTILITY_H
#define UTILITY_H

#include <QImage>

QT_BEGIN_NAMESPACE
class QXcbWindow;
QT_END_NAMESPACE

typedef uint32_t xcb_cursor_t;

class Utility
{
public:
    enum CornerEdge {
        TopLeftCorner = 0,
        TopEdge = 1,
        TopRightCorner = 2,
        RightEdge = 3,
        BottomRightCorner = 4,
        BottomEdge = 5,
        BottomLeftCorner = 6,
        LeftEdge = 7
    };

    static QImage dropShadow(const QPixmap &px, qreal radius, const QColor &color);
    static QImage borderImage(const QPixmap &px, const QMargins &borders, const QSize &size,
                              QImage::Format format = QImage::Format_ARGB32_Premultiplied);

    static QList<QRect> sudokuByRect(const QRect &rect, QMargins borders);

    static void startWindowSystemMove(uint WId);
    static void cancelWindowMoveResize(uint WId);
    static void setFrameExtents(uint WId, const QMargins &margins);
    static void setInputShapeRectangles(uint WId, const QRegion &region);
    static void startWindowSystemResize(uint WId, CornerEdge cornerEdge, const QPoint &globalPos = QPoint());
    static bool setWindowCursor(uint WId, CornerEdge ce);

    static QRegion regionAddMargins(const QRegion &region, const QMargins &margins, const QPoint &offset = QPoint(0, 0));

private:
    static void sendMoveResizeMessage(uint WId, uint32_t action, QPoint globalPos = QPoint(), Qt::MouseButton qbutton = Qt::LeftButton);
};

#endif // UTILITY_H
