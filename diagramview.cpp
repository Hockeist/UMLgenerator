#include "diagramview.h"

#include <QBrush>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QImage>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRectF>
#include <QScrollBar>
#include <QTextStream>
#include <QVector>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <string>

namespace
{

struct ClassBox
{
    QString name;
    QRectF rect;
    QPointF initialPos;
    QVector<QString> attributes;
    QVector<QString> methods;
};

using Route = QVector<QPointF>;
using UsedSegments = QVector<QLineF>;

QString toQString(const std::string& value)
{
    return QString::fromStdString(value);
}

QString accessPrefix(UmlAccess access)
{
    switch (access)
    {
    case UmlAccess::Public:    return "+ ";
    case UmlAccess::Protected: return "# ";
    case UmlAccess::Private:   return "- ";
    case UmlAccess::Unknown:
    default:                   return "";
    }
}

QString formatMember(const UmlMember& member)
{
    return accessPrefix(member.access) + toQString(member.text);
}

// Интерактивная карточка UML-класса, которую можно двигать мышкой
class DraggableClassItem : public QGraphicsRectItem
{
public:
    DraggableClassItem(const QRectF& localRect, DiagramView* view)
        : QGraphicsRectItem(localRect)
        , m_view(view)
    {
        setFlags(QGraphicsItem::ItemIsMovable |
                 QGraphicsItem::ItemIsSelectable |
                 QGraphicsItem::ItemSendsGeometryChanges);

        setCursor(Qt::OpenHandCursor);
        setZValue(2.0);
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override
    {
        if (change == ItemPositionHasChanged && m_view)
        {
            m_view->rebuildRelationships();
        }
        return QGraphicsRectItem::itemChange(change, value);
    }

private:
    DiagramView* m_view = nullptr;
};

enum class PortSide
{
    Top,
    Bottom,
    Left,
    Right
};

struct PortInfo
{
    QPointF point;
    PortSide side;
};

// Возвращает точки подключения на гранях блока.
// МЫ РАЗДВИНУЛИ боковые точки (0.20 и 0.80 вместо 0.30 и 0.70),
// чтобы соседние стрелки выходили из блока далеко друг от друга!
QVector<PortInfo> candidatePorts(const QRectF& rect)
{
    QVector<PortInfo> ports;
    const qreal cx = rect.center().x();
    const qreal cy = rect.center().y();

    // Центральные порты
    ports.push_back({QPointF(cx, rect.top()), PortSide::Top});
    ports.push_back({QPointF(cx, rect.bottom()), PortSide::Bottom});
    ports.push_back({QPointF(rect.left(), cy), PortSide::Left});
    ports.push_back({QPointF(rect.right(), cy), PortSide::Right});

    // Боковые порты — широко разнесены по краям карточки
    ports.push_back({QPointF(rect.left() + rect.width() * 0.20, rect.top()), PortSide::Top});
    ports.push_back({QPointF(rect.left() + rect.width() * 0.80, rect.top()), PortSide::Top});
    ports.push_back({QPointF(rect.left() + rect.width() * 0.20, rect.bottom()), PortSide::Bottom});
    ports.push_back({QPointF(rect.left() + rect.width() * 0.80, rect.bottom()), PortSide::Bottom});

    ports.push_back({QPointF(rect.left(), rect.top() + rect.height() * 0.25), PortSide::Left});
    ports.push_back({QPointF(rect.left(), rect.top() + rect.height() * 0.75), PortSide::Left});
    ports.push_back({QPointF(rect.right(), rect.top() + rect.height() * 0.25), PortSide::Right});
    ports.push_back({QPointF(rect.right(), rect.top() + rect.height() * 0.75), PortSide::Right});

    return ports;
}

QPointF outwardVector(PortSide side)
{
    switch (side)
    {
    case PortSide::Top:    return QPointF(0, -1);
    case PortSide::Bottom: return QPointF(0, 1);
    case PortSide::Left:   return QPointF(-1, 0);
    case PortSide::Right:  return QPointF(1, 0);
    }
    return QPointF(0, -1);
}

QPointF outwardStub(const PortInfo& port, qreal distance)
{
    return port.point + outwardVector(port.side) * distance;
}

Route simplifyRoute(const Route& input)
{
    Route result;
    for (const QPointF& point : input)
    {
        if (!result.isEmpty() && QLineF(result.last(), point).length() < 0.5)
            continue;

        while (result.size() >= 2)
        {
            const QPointF a = result[result.size() - 2];
            const QPointF b = result[result.size() - 1];

            const bool sameX = qAbs(a.x() - b.x()) < 0.5 && qAbs(b.x() - point.x()) < 0.5;
            const bool sameY = qAbs(a.y() - b.y()) < 0.5 && qAbs(b.y() - point.y()) < 0.5;

            if (sameX || sameY)
                result.removeLast();
            else
                break;
        }
        result.push_back(point);
    }
    return result;
}

qreal routeLength(const Route& route)
{
    qreal length = 0.0;
    for (int i = 1; i < route.size(); ++i)
        length += QLineF(route[i - 1], route[i]).length();
    return length;
}

bool segmentIntersectsRect(const QPointF& a, const QPointF& b, const QRectF& rect)
{
    constexpr qreal epsilon = 0.5;
    if (qAbs(a.y() - b.y()) < epsilon)
    {
        const qreal left = qMin(a.x(), b.x());
        const qreal right = qMax(a.x(), b.x());
        return a.y() >= rect.top() - epsilon &&
               a.y() <= rect.bottom() + epsilon &&
               right >= rect.left() - epsilon &&
               left <= rect.right() + epsilon;
    }
    if (qAbs(a.x() - b.x()) < epsilon)
    {
        const qreal top = qMin(a.y(), b.y());
        const qreal bottom = qMax(a.y(), b.y());
        return a.x() >= rect.left() - epsilon &&
               a.x() <= rect.right() + epsilon &&
               bottom >= rect.top() - epsilon &&
               top <= rect.bottom() + epsilon;
    }
    return rect.intersects(QRectF(a, b).normalized());
}

// ПАРАМЕТР РАССТОЯНИЯ МЕЖДУ ЛИНИЯМИ:
// Переменная minLineSpacing = 18.0 заставляет параллельные линии держаться
// минимум в 18 пикселях друг от друга!
qreal segmentCrossingPenalty(const QLineF& a, const QLineF& b)
{
    constexpr qreal epsilon = 1.0;
    constexpr qreal minLineSpacing = 18.0; // Было 4.0, стало 18.0!

    const bool horizontalA = qAbs(a.y1() - a.y2()) < epsilon;
    const bool horizontalB = qAbs(b.y1() - b.y2()) < epsilon;
    const bool verticalA   = qAbs(a.x1() - a.x2()) < epsilon;
    const bool verticalB   = qAbs(b.x1() - b.x2()) < epsilon;

    // Если две горизонтальные линии подходят ближе чем на 18 пикселей — большой штраф!
    if (horizontalA && horizontalB && qAbs(a.y1() - b.y1()) < minLineSpacing)
    {
        const qreal minAx = qMin(a.x1(), a.x2()) - 5.0;
        const qreal maxAx = qMax(a.x1(), a.x2()) + 5.0;
        const qreal minBx = qMin(b.x1(), b.x2()) - 5.0;
        const qreal maxBx = qMax(b.x1(), b.x2()) + 5.0;
        if (maxAx > minBx && minAx < maxBx)
            return 6000.0;
    }

    // Если две вертикальные линии подходят ближе чем на 18 пикселей — большой штраф!
    if (verticalA && verticalB && qAbs(a.x1() - b.x1()) < minLineSpacing)
    {
        const qreal minAy = qMin(a.y1(), a.y2()) - 5.0;
        const qreal maxAy = qMax(a.y1(), a.y2()) + 5.0;
        const qreal minBy = qMin(b.y1(), b.y2()) - 5.0;
        const qreal maxBy = qMax(b.y1(), b.y2()) + 5.0;
        if (maxAy > minBy && minAy < maxBy)
            return 6000.0;
    }

    QPointF intersection;
    if (a.intersects(b, &intersection) != QLineF::BoundedIntersection)
        return 0.0;

    const bool touchesEndpoint =
        QLineF(intersection, a.p1()).length() < epsilon ||
        QLineF(intersection, a.p2()).length() < epsilon ||
        QLineF(intersection, b.p1()).length() < epsilon ||
        QLineF(intersection, b.p2()).length() < epsilon;

    return touchesEndpoint ? 0.0 : 5000.0;
}

qreal portDirectionPenalty(const QRectF& fromRect, const QRectF& toRect, PortSide side)
{
    const QPointF delta = toRect.center() - fromRect.center();
    const QPointF dir = outwardVector(side);
    const qreal dot = delta.x() * dir.x() + delta.y() * dir.y();

    if (dot < -10.0)
        return 600.0;
    if (dot < 10.0)
        return 180.0;
    return 0.0;
}

qreal routeScore(const Route& route, const QVector<QRectF>& obstacles, const UsedSegments& usedSegments)
{
    constexpr qreal collisionPenalty = 10000000.0;
    constexpr qreal bendPenalty = 90.0;

    qreal score = routeLength(route);
    if (route.size() > 2)
        score += (route.size() - 2) * bendPenalty;

    for (int i = 1; i < route.size(); ++i)
    {
        const QLineF segment(route[i - 1], route[i]);
        for (const QRectF& obstacle : obstacles)
        {
            if (segmentIntersectsRect(segment.p1(), segment.p2(), obstacle))
                score += collisionPenalty;
        }
        for (const QLineF& used : usedSegments)
            score += segmentCrossingPenalty(segment, used);
    }
    return score;
}

Route findRoute(const QPointF& start, const QPointF& end, const QVector<QRectF>& obstacles, const UsedSegments& usedSegments)
{
    QVector<Route> candidates;
    const qreal middleX = (start.x() + end.x()) / 2.0;
    const qreal middleY = (start.y() + end.y()) / 2.0;

    candidates.push_back(Route{start, QPointF(end.x(), start.y()), end});
    candidates.push_back(Route{start, QPointF(start.x(), end.y()), end});
    candidates.push_back(Route{start, QPointF(middleX, start.y()), QPointF(middleX, end.y()), end});
    candidates.push_back(Route{start, QPointF(start.x(), middleY), QPointF(end.x(), middleY), end});

    // Увеличили отступ обхода вокруг блоков с 28 до 38 пикселей
    constexpr qreal detourGap = 38.0;
    for (const QRectF& obstacle : obstacles)
    {
        const qreal left = obstacle.left() - detourGap;
        const qreal right = obstacle.right() + detourGap;
        const qreal top = obstacle.top() - detourGap;
        const qreal bottom = obstacle.bottom() + detourGap;

        candidates.push_back(Route{start, QPointF(left, start.y()), QPointF(left, end.y()), end});
        candidates.push_back(Route{start, QPointF(right, start.y()), QPointF(right, end.y()), end});
        candidates.push_back(Route{start, QPointF(start.x(), top), QPointF(end.x(), top), end});
        candidates.push_back(Route{start, QPointF(start.x(), bottom), QPointF(end.x(), bottom), end});
    }

    // Добавили больше вариантов параллельных дорожек с шагом 24 пикселя (24, 48, 72, 96)
    for (qreal offset : {24.0, 48.0, 72.0, 96.0})
    {
        candidates.push_back(Route{start, QPointF(start.x(), middleY - offset), QPointF(end.x(), middleY - offset), end});
        candidates.push_back(Route{start, QPointF(start.x(), middleY + offset), QPointF(end.x(), middleY + offset), end});
        candidates.push_back(Route{start, QPointF(middleX - offset, start.y()), QPointF(middleX - offset, end.y()), end});
        candidates.push_back(Route{start, QPointF(middleX + offset, start.y()), QPointF(middleX + offset, end.y()), end});
    }

    Route bestRoute;
    qreal bestScore = 1e100;
    for (const Route& candidate : candidates)
    {
        const Route simplified = simplifyRoute(candidate);
        const qreal score = routeScore(simplified, obstacles, usedSegments);
        if (score < bestScore)
        {
            bestScore = score;
            bestRoute = simplified;
        }
    }
    return bestRoute;
}

void rememberRoute(const Route& route, UsedSegments& usedSegments)
{
    for (int i = 1; i < route.size(); ++i)
        usedSegments.push_back(QLineF(route[i - 1], route[i]));
}

Route buildConnectionRoute(const QRectF& source, const QRectF& target,
                           const QVector<QRectF>& obstacles, const UsedSegments& usedSegments,
                           PortInfo& bestSrcPort, PortInfo& bestTgtPort)
{
    const QVector<PortInfo> srcPorts = candidatePorts(source);
    const QVector<PortInfo> tgtPorts = candidatePorts(target);

    Route bestRoute;
    qreal bestScore = 1e100;
    bestSrcPort = srcPorts.first();
    bestTgtPort = tgtPorts.first();

    constexpr qreal stubDistance = 26.0;

    QVector<QRectF> allObstacles = obstacles;
    allObstacles.push_back(source.adjusted(-6.0, -6.0, 6.0, 6.0));
    allObstacles.push_back(target.adjusted(-6.0, -6.0, 6.0, 6.0));

    for (const PortInfo& sPort : srcPorts)
    {
        const qreal sPenalty = portDirectionPenalty(source, target, sPort.side);
        for (const PortInfo& tPort : tgtPorts)
        {
            const qreal tPenalty = portDirectionPenalty(target, source, tPort.side);
            const QPointF sStub = outwardStub(sPort, stubDistance);
            const QPointF tStub = outwardStub(tPort, stubDistance);

            Route middle = findRoute(sStub, tStub, allObstacles, usedSegments);

            Route current;
            current.push_back(sPort.point);
            current.push_back(sStub);
            for (int i = 1; i < middle.size(); ++i)
                current.push_back(middle[i]);
            current.push_back(tPort.point);

            Route simplified = simplifyRoute(current);
            qreal score = routeScore(simplified, obstacles, usedSegments) + sPenalty + tPenalty;

            for (int i = 1; i + 1 < simplified.size(); ++i)
            {
                if (segmentIntersectsRect(simplified[i - 1], simplified[i], source.adjusted(2, 2, -2, -2)) ||
                    segmentIntersectsRect(simplified[i], simplified[i + 1], target.adjusted(2, 2, -2, -2)))
                {
                    score += 10000000.0;
                }
            }

            if (score < bestScore)
            {
                bestScore = score;
                bestRoute = simplified;
                bestSrcPort = sPort;
                bestTgtPort = tPort;
            }
        }
    }
    return bestRoute;
}

// Вычисляет позицию для увеличенных цифр кратности ("1", "0..*"), чтобы они не налезали на линии
QPointF multiplicityLabelPos(const PortInfo& port, const QSizeF& textSize)
{
    const QPointF pt = port.point;
    switch (port.side)
    {
    case PortSide::Top:
        return QPointF(pt.x() + 10.0, pt.y() - textSize.height() - 6.0);
    case PortSide::Bottom:
        return QPointF(pt.x() + 10.0, pt.y() + 6.0);
    case PortSide::Left:
        return QPointF(pt.x() - textSize.width() - 10.0, pt.y() - textSize.height() - 4.0);
    case PortSide::Right:
        return QPointF(pt.x() + 10.0, pt.y() - textSize.height() - 4.0);
    }
    return pt;
}

// Отрисовка связей и увеличенных цифр кратности
void drawRelationship(QGraphicsScene* scene,
                      const UmlRelationship& rel,
                      const QRectF& source,
                      const QRectF& target,
                      const QVector<QRectF>& obstacles,
                      UsedSegments& usedSegments,
                      QVector<QGraphicsItem*>& outItems)
{
    PortInfo srcPort;
    PortInfo tgtPort;

    Route route = buildConnectionRoute(
        source, target, obstacles, usedSegments, srcPort, tgtPort);

    if (route.size() < 2)
        return;

    const RelationshipType type = rel.type();
    const QColor lineColor("#344054");
    QPen linePen(lineColor, 1.8);
    linePen.setCapStyle(Qt::RoundCap);
    linePen.setJoinStyle(Qt::RoundJoin);

    if (type == RelationshipType::Dependency)
        linePen.setStyle(Qt::DashLine);

    QPen symbolPen(lineColor, 1.8);
    symbolPen.setCapStyle(Qt::RoundCap);
    symbolPen.setJoinStyle(Qt::MiterJoin);

    if (type == RelationshipType::Composition || type == RelationshipType::Aggregation)
    {
        const QPointF outward = outwardVector(srcPort.side);
        const QPointF perp(-outward.y(), outward.x());

        constexpr qreal diamondLength = 18.0;
        constexpr qreal diamondHalfWidth = 6.5;

        const QPointF tip = srcPort.point;
        const QPointF mid = tip + outward * (diamondLength * 0.5);
        const QPointF back = tip + outward * diamondLength;

        route[0] = back;
        route = simplifyRoute(route);

        QPolygonF diamond;
        diamond << tip << (mid + perp * diamondHalfWidth) << back << (mid - perp * diamondHalfWidth);

        const QBrush brush = (type == RelationshipType::Composition) ? QBrush(lineColor) : QBrush(Qt::white);
        auto* polyItem = scene->addPolygon(diamond, symbolPen, brush);
        polyItem->setZValue(4.5);
        outItems.push_back(polyItem);
    }

    if (type == RelationshipType::Inheritance)
    {
        const QPointF outward = outwardVector(tgtPort.side);
        const QPointF perp(-outward.y(), outward.x());

        constexpr qreal arrowLength = 16.0;
        constexpr qreal arrowHalfWidth = 8.5;

        const QPointF tip = tgtPort.point;
        const QPointF base = tip + outward * arrowLength;

        route.last() = base;
        route = simplifyRoute(route);

        QPolygonF triangle;
        triangle << tip << (base + perp * arrowHalfWidth) << (base - perp * arrowHalfWidth);

        auto* triItem = scene->addPolygon(triangle, symbolPen, QBrush(Qt::white));
        triItem->setZValue(4.5);
        outItems.push_back(triItem);
    }

    if (type == RelationshipType::Association || type == RelationshipType::Dependency)
    {
        const QPointF outward = outwardVector(tgtPort.side);
        const QPointF perp(-outward.y(), outward.x());

        constexpr qreal wingLength = 12.0;
        constexpr qreal wingHalfWidth = 6.0;

        const QPointF tip = tgtPort.point;
        const QPointF wingBase = tip + outward * wingLength;

        QPainterPath arrowPath;
        arrowPath.moveTo(wingBase + perp * wingHalfWidth);
        arrowPath.lineTo(tip);
        arrowPath.lineTo(wingBase - perp * wingHalfWidth);

        auto* arrowItem = scene->addPath(arrowPath, symbolPen);
        arrowItem->setZValue(4.5);
        outItems.push_back(arrowItem);
    }

    if (route.size() >= 2)
    {
        QPainterPath path;
        path.moveTo(route.first());
        for (int i = 1; i < route.size(); ++i)
            path.lineTo(route[i]);

        auto* pathItem = scene->addPath(path, linePen);
        pathItem->setZValue(1.0);
        outItems.push_back(pathItem);

        rememberRoute(route, usedSegments);
    }

    // УВЕЛИЧЕННЫЙ ШРИФТ для цифр кратности ("1", "0..*", "0..1") — теперь 12pt Bold!
    const QFont multFont("Arial", 12, QFont::Bold);

    if (!rel.sourceMultiplicity().empty())
    {
        auto* srcLabel = scene->addText(toQString(rel.sourceMultiplicity()), multFont);
        srcLabel->setDefaultTextColor(QColor("#101828"));
        srcLabel->setPos(multiplicityLabelPos(srcPort, srcLabel->boundingRect().size()));
        srcLabel->setZValue(5.0);
        outItems.push_back(srcLabel);
    }

    if (!rel.targetMultiplicity().empty())
    {
        auto* tgtLabel = scene->addText(toQString(rel.targetMultiplicity()), multFont);
        tgtLabel->setDefaultTextColor(QColor("#101828"));
        tgtLabel->setPos(multiplicityLabelPos(tgtPort, tgtLabel->boundingRect().size()));
        tgtLabel->setZValue(5.0);
        outItems.push_back(tgtLabel);
    }
}

} // namespace

// -----------------------------------------------------------------------------
// Реализация методов класса DiagramView
// -----------------------------------------------------------------------------

DiagramView::DiagramView(QWidget* parent)
    : QGraphicsView(parent)
{
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::TextAntialiasing);
    setMinimumSize(600, 400);

    auto* diagramScene = new QGraphicsScene(this);
    setScene(diagramScene);

    setBackgroundBrush(QColor("#F7F8FA"));
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDragMode(QGraphicsView::ScrollHandDrag);
}

void DiagramView::setDiagram(const UmlDiagram& diagram)
{
    m_diagram = diagram;
    drawDiagram();
}

// Полная очистка сцены по нажатию на кнопку «Очистить сцену»
void DiagramView::clearDiagram()
{
    m_diagram = UmlDiagram();
    m_classItems.clear();
    m_relationshipItems.clear();

    if (QGraphicsScene* diagramScene = scene())
    {
        diagramScene->clear();
        diagramScene->setSceneRect(QRectF(0, 0, 800, 600));
    }

    resetTransform();
}

void DiagramView::wheelEvent(QWheelEvent* event)
{
    const qreal scaleFactor = 1.15;
    const qreal currentScale = transform().m11();

    if (event->angleDelta().y() > 0)
    {
        if (currentScale < 4.0)
            scale(scaleFactor, scaleFactor);
    }
    else if (event->angleDelta().y() < 0)
    {
        if (currentScale > 0.2)
            scale(1.0 / scaleFactor, 1.0 / scaleFactor);
    }
    event->accept();
}

void DiagramView::mousePressEvent(QMouseEvent* event)
{
    QGraphicsItem* itemUnderMouse = itemAt(event->pos());

    if (event->button() == Qt::MiddleButton ||
        event->button() == Qt::RightButton ||
        (event->button() == Qt::LeftButton && itemUnderMouse == nullptr))
    {
        m_isPanning = true;
        m_lastPanPoint = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void DiagramView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanning)
    {
        const QPoint delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();

        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void DiagramView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isPanning)
    {
        m_isPanning = false;
        viewport()->setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void DiagramView::clearRelationshipItems()
{
    QGraphicsScene* diagramScene = scene();
    if (!diagramScene)
        return;

    for (QGraphicsItem* item : m_relationshipItems)
    {
        diagramScene->removeItem(item);
        delete item;
    }
    m_relationshipItems.clear();
}

void DiagramView::rebuildRelationships()
{
    QGraphicsScene* diagramScene = scene();
    if (!diagramScene)
        return;

    clearRelationshipItems();

    QMap<QString, QRectF> classRects;
    QVector<QRectF> allRects;

    for (auto it = m_classItems.begin(); it != m_classItems.end(); ++it)
    {
        const QRectF sceneRect = it.value()->sceneBoundingRect();
        classRects[it.key()] = sceneRect;
        allRects.push_back(sceneRect);
    }

    QVector<const UmlRelationship*> orderedRelationships;
    for (const UmlRelationship& relationship : m_diagram.relationships())
    {
        if (relationship.type() == RelationshipType::Inheritance)
            orderedRelationships.prepend(&relationship);
        else
            orderedRelationships.push_back(&relationship);
    }

    UsedSegments usedSegments;

    for (const UmlRelationship* relationship : orderedRelationships)
    {
        const QString sourceName = toQString(relationship->source());
        const QString targetName = toQString(relationship->target());

        if (!classRects.contains(sourceName) || !classRects.contains(targetName))
            continue;

        const QRectF sourceRect = classRects[sourceName];
        const QRectF targetRect = classRects[targetName];

        QVector<QRectF> obstacles;
        for (const QRectF& rect : allRects)
        {
            if (rect == sourceRect || rect == targetRect)
                continue;
            obstacles.push_back(rect.adjusted(-12.0, -12.0, 12.0, 12.0));
        }

        drawRelationship(
            diagramScene,
            *relationship,
            sourceRect,
            targetRect,
            obstacles,
            usedSegments,
            m_relationshipItems);
    }

    diagramScene->setSceneRect(
        diagramScene->itemsBoundingRect().adjusted(-600.0, -600.0, 600.0, 600.0));
}

void DiagramView::drawDiagram()
{
    QGraphicsScene* diagramScene = scene();
    if (!diagramScene)
        return;

    m_classItems.clear();
    m_relationshipItems.clear();
    diagramScene->clear();
    resetTransform();

    const QFont titleFont("Arial", 12, QFont::Bold);
    const QFont memberFont("Arial", 10);

    const QFontMetricsF titleMetrics(titleFont);
    const QFontMetricsF memberMetrics(memberFont);

    constexpr qreal padding = 20.0;
    constexpr qreal headerHeight = 42.0;
    constexpr qreal rowHeight = 25.0;
    constexpr qreal minimumWidth = 230.0;

    constexpr int maxColumnsPerRow = 3;
    constexpr qreal columnGap = 150.0;
    constexpr qreal levelGap = 150.0;

    QVector<ClassBox> boxes;

    for (const UmlClass& umlClass : m_diagram.classes())
    {
        ClassBox box;
        box.name = toQString(umlClass.name());

        qreal contentWidth = titleMetrics.horizontalAdvance(box.name) + 24.0;

        for (const UmlMember& attribute : umlClass.attributeMembers())
        {
            const QString text = formatMember(attribute);
            box.attributes.push_back(text);
            contentWidth = qMax(contentWidth, memberMetrics.horizontalAdvance(text));
        }

        for (const UmlMember& method : umlClass.methodMembers())
        {
            const QString text = formatMember(method);
            box.methods.push_back(text);
            contentWidth = qMax(contentWidth, memberMetrics.horizontalAdvance(text));
        }

        // Увеличенный запас ширины карточки (contentWidth * 1.28 + 44px),
        // чтобы в любом браузере или просмотрщике SVG текст легко помещался внутри блока!
        const qreal boxWidth = qMax(minimumWidth, contentWidth * 1.28 + padding * 2.2);
        const qreal attributesHeight = qMax(14.0, static_cast<qreal>(box.attributes.size()) * rowHeight + 8.0);
        const qreal methodsHeight = qMax(14.0, static_cast<qreal>(box.methods.size()) * rowHeight + 8.0);
        const qreal boxHeight = headerHeight + attributesHeight + methodsHeight;

        box.rect = QRectF(0, 0, boxWidth, boxHeight);
        boxes.push_back(box);
    }

    const int classCount = boxes.size();
    if (classCount == 0)
    {
        diagramScene->setSceneRect(QRectF(0, 0, 800, 600));
        return;
    }

    QMap<QString, int> classIndex;
    for (int i = 0; i < classCount; ++i)
        classIndex[boxes[i].name] = i;

    QVector<QVector<int>> parents(classCount);
    QVector<int> connectionCount(classCount, 0);

    for (const UmlRelationship& rel : m_diagram.relationships())
    {
        const QString sName = toQString(rel.source());
        const QString tName = toQString(rel.target());
        if (!classIndex.contains(sName) || !classIndex.contains(tName))
            continue;

        const int sIdx = classIndex[sName];
        const int tIdx = classIndex[tName];
        connectionCount[sIdx]++;
        connectionCount[tIdx]++;

        if (rel.type() == RelationshipType::Inheritance && sIdx != tIdx)
        {
            if (!parents[sIdx].contains(tIdx))
                parents[sIdx].push_back(tIdx);
        }
    }

    QVector<int> levels(classCount, -1);
    QVector<int> visitState(classCount, 0);

    std::function<int(int)> calculateLevel = [&](int index) -> int
    {
        if (visitState[index] == 2)
            return levels[index];
        if (visitState[index] == 1)
            return 0;

        visitState[index] = 1;
        int level = 0;
        for (int parent : parents[index])
            level = qMax(level, calculateLevel(parent) + 1);

        visitState[index] = 2;
        levels[index] = level;
        return level;
    };

    for (int i = 0; i < classCount; ++i)
        calculateLevel(i);

    QMap<int, QVector<int>> levelGroups;
    for (int i = 0; i < classCount; ++i)
        levelGroups[levels[i]].push_back(i);

    QVector<QVector<int>> visualRows;
    for (auto it = levelGroups.begin(); it != levelGroups.end(); ++it)
    {
        QVector<int> group = it.value();

        std::sort(group.begin(), group.end(), [&](int a, int b)
                  {
                      if (connectionCount[a] != connectionCount[b])
                          return connectionCount[a] > connectionCount[b];
                      return boxes[a].name.toLower() < boxes[b].name.toLower();
                  });

        for (int i = 0; i < group.size(); i += maxColumnsPerRow)
        {
            QVector<int> subRow;
            for (int j = i; j < qMin(static_cast<int>(group.size()), i + maxColumnsPerRow); ++j)
                subRow.push_back(group[j]);
            visualRows.push_back(subRow);
        }
    }

    QVector<qreal> rowWidths(visualRows.size(), 0.0);
    qreal maxTotalWidth = 0.0;

    for (int r = 0; r < visualRows.size(); ++r)
    {
        qreal w = 0.0;
        for (int idx : visualRows[r])
            w += boxes[idx].rect.width();
        if (!visualRows[r].isEmpty())
            w += (visualRows[r].size() - 1) * columnGap;

        rowWidths[r] = w;
        maxTotalWidth = qMax(maxTotalWidth, w);
    }

    qreal currentY = 60.0;
    for (int r = 0; r < visualRows.size(); ++r)
    {
        const QVector<int>& row = visualRows[r];
        qreal currentX = 60.0 + (maxTotalWidth - rowWidths[r]) / 2.0;
        qreal rowMaxHeight = 0.0;

        for (int idx : row)
        {
            boxes[idx].initialPos = QPointF(currentX, currentY);
            currentX += boxes[idx].rect.width() + columnGap;
            rowMaxHeight = qMax(rowMaxHeight, boxes[idx].rect.height());
        }

        currentY += rowMaxHeight + levelGap;
    }

    for (const ClassBox& box : boxes)
    {
        const QRectF localRect = box.rect;
        const QColor borderColor("#344054");
        const QColor headerColor("#E8F0FC");
        const QPen borderPen(borderColor, 1.5);

        auto* card = new DraggableClassItem(localRect, this);
        card->setPen(borderPen);
        card->setBrush(QBrush(Qt::white));
        diagramScene->addItem(card);

        const QRectF headerRect(0, 0, localRect.width(), headerHeight);
        auto* header = new QGraphicsRectItem(headerRect, card);
        header->setPen(QPen(Qt::NoPen));
        header->setBrush(QBrush(headerColor));

        auto* outline = new QGraphicsRectItem(localRect, card);
        outline->setPen(borderPen);
        outline->setBrush(QBrush(Qt::NoBrush));

        auto* title = new QGraphicsTextItem(box.name, card);
        title->setFont(titleFont);
        title->setDefaultTextColor(QColor("#172B4D"));
        title->setPos(
            localRect.width() / 2.0 - title->boundingRect().width() / 2.0,
            (headerHeight - title->boundingRect().height()) / 2.0);

        const qreal attributesHeight = qMax(
            14.0, static_cast<qreal>(box.attributes.size()) * rowHeight + 8.0);

        qreal textY = headerHeight;
        auto* sep1 = new QGraphicsLineItem(0, textY, localRect.width(), textY, card);
        sep1->setPen(QPen(borderColor, 1.0));

        textY += 5.0;
        for (const QString& attribute : box.attributes)
        {
            auto* text = new QGraphicsTextItem(attribute, card);
            text->setFont(memberFont);
            text->setDefaultTextColor(QColor("#202939"));
            text->setPos(padding, textY);
            textY += rowHeight;
        }

        textY = headerHeight + attributesHeight;
        auto* sep2 = new QGraphicsLineItem(0, textY, localRect.width(), textY, card);
        sep2->setPen(QPen(borderColor, 1.0));

        textY += 5.0;
        for (const QString& method : box.methods)
        {
            auto* text = new QGraphicsTextItem(method, card);
            text->setFont(memberFont);
            text->setDefaultTextColor(QColor("#202939"));
            text->setPos(padding, textY);
            textY += rowHeight;
        }

        card->setPos(box.initialPos);
        m_classItems[box.name] = card;
    }

    rebuildRelationships();

    const QRectF contentRect = diagramScene->itemsBoundingRect().adjusted(-40.0, -40.0, 40.0, 40.0);
    fitInView(contentRect, Qt::KeepAspectRatio);

    if (transform().m11() > 1.0)
        resetTransform();

    centerOn(contentRect.center());
}

bool DiagramView::exportToPng(const QString& filePath)
{
    QGraphicsScene* diagramScene = scene();
    if (!diagramScene || filePath.isEmpty())
        return false;

    const QRectF bounds = diagramScene->itemsBoundingRect().adjusted(-35.0, -35.0, 35.0, 35.0);
    if (bounds.isEmpty())
        return false;

    const QSize imageSize(qCeil(bounds.width() * 2.0), qCeil(bounds.height() * 2.0));
    QImage image(imageSize, QImage::Format_ARGB32);
    image.fill(QColor("#F7F8FA"));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    diagramScene->render(&painter, QRectF(0, 0, imageSize.width(), imageSize.height()), bounds);
    painter.end();

    return image.save(filePath, "PNG");
}

// Экспорт в SVG с жесткой защитой (<clipPath>) от выхода текста за границы карточек
bool DiagramView::exportToSvg(const QString& filePath)
{
    QGraphicsScene* diagramScene = scene();
    if (!diagramScene || filePath.isEmpty())
        return false;

    const QRectF bounds = diagramScene->itemsBoundingRect().adjusted(-35.0, -35.0, 35.0, 35.0);
    if (bounds.isEmpty())
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);

    auto escapeXml = [](QString text) -> QString
    {
        text.replace("&", "&amp;");
        text.replace("<", "&lt;");
        text.replace(">", "&gt;");
        text.replace("\"", "&quot;");
        return text;
    };

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
    out << QString("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"%3 %4 %5 %6\">\n")
               .arg(qCeil(bounds.width()))
               .arg(qCeil(bounds.height()))
               .arg(bounds.left())
               .arg(bounds.top())
               .arg(bounds.width())
               .arg(bounds.height());

    // 1. Создаем секцию <defs> с областями обрезки (clipPath) для каждой карточки класса,
    // чтобы текст ни при каких условиях не мог вылезти за правую границу блока!
    out << "  <defs>\n";
    QMap<QGraphicsItem*, int> cardClipIds;
    int clipCounter = 1;

    for (auto it = m_classItems.begin(); it != m_classItems.end(); ++it)
    {
        QGraphicsItem* cardItem = it.value();
        const QRectF r = cardItem->sceneBoundingRect().adjusted(2.0, 2.0, -4.0, -2.0);
        const int id = clipCounter++;
        cardClipIds[cardItem] = id;

        out << QString("    <clipPath id=\"cardClip%1\">\n"
                       "      <rect x=\"%2\" y=\"%3\" width=\"%4\" height=\"%5\"/>\n"
                       "    </clipPath>\n")
                   .arg(id)
                   .arg(r.left())
                   .arg(r.top())
                   .arg(r.width())
                   .arg(r.height());
    }
    out << "  </defs>\n";

    // Фон диаграммы
    out << QString("  <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" fill=\"#F7F8FA\"/>\n")
               .arg(bounds.left())
               .arg(bounds.top())
               .arg(bounds.width())
               .arg(bounds.height());

    const QList<QGraphicsItem*> allItems = diagramScene->items(Qt::AscendingOrder);

    for (QGraphicsItem* item : allItems)
    {
        if (auto* pathItem = dynamic_cast<QGraphicsPathItem*>(item))
        {
            const QPainterPath path = pathItem->sceneTransform().map(pathItem->path());
            QString d;
            for (int i = 0; i < path.elementCount(); ++i)
            {
                const QPainterPath::Element el = path.elementAt(i);
                if (el.isMoveTo())
                    d += QString("M %1 %2 ").arg(el.x).arg(el.y);
                else if (el.isLineTo())
                    d += QString("L %1 %2 ").arg(el.x).arg(el.y);
            }

            const QPen pen = pathItem->pen();
            const QString dash = (pen.style() == Qt::DashLine) ? " stroke-dasharray=\"6,4\"" : "";

            out << QString("  <path d=\"%1\" fill=\"none\" stroke=\"%2\" stroke-width=\"%3\" stroke-linecap=\"round\" stroke-linejoin=\"round\"%4/>\n")
                       .arg(d.trimmed())
                       .arg(pen.color().name())
                       .arg(pen.widthF())
                       .arg(dash);
        }
        else if (auto* polyItem = dynamic_cast<QGraphicsPolygonItem*>(item))
        {
            const QPolygonF poly = polyItem->sceneTransform().map(polyItem->polygon());
            QString pts;
            for (const QPointF& pt : poly)
                pts += QString("%1,%2 ").arg(pt.x()).arg(pt.y());

            const QPen pen = polyItem->pen();
            const QBrush brush = polyItem->brush();
            const QString fill = (brush.style() == Qt::NoBrush) ? "none" : brush.color().name();

            out << QString("  <polygon points=\"%1\" fill=\"%2\" stroke=\"%3\" stroke-width=\"%4\"/>\n")
                       .arg(pts.trimmed())
                       .arg(fill)
                       .arg(pen.color().name())
                       .arg(pen.widthF());
        }
        else if (auto* rectItem = dynamic_cast<QGraphicsRectItem*>(item))
        {
            const QRectF r = rectItem->sceneTransform().mapRect(rectItem->rect());
            const QPen pen = rectItem->pen();
            const QBrush brush = rectItem->brush();

            const QString fill = (brush.style() == Qt::NoBrush) ? "none" : brush.color().name();
            const QString stroke = (pen.style() == Qt::NoPen) ? "none" : pen.color().name();

            out << QString("  <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" fill=\"%5\" stroke=\"%6\" stroke-width=\"%7\"/>\n")
                       .arg(r.left())
                       .arg(r.top())
                       .arg(r.width())
                       .arg(r.height())
                       .arg(fill)
                       .arg(stroke)
                       .arg(pen.widthF());
        }
        else if (auto* lineItem = dynamic_cast<QGraphicsLineItem*>(item))
        {
            const QLineF l = lineItem->sceneTransform().map(lineItem->line());
            const QPen pen = lineItem->pen();

            out << QString("  <line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%4\" stroke=\"%5\" stroke-width=\"%6\"/>\n")
                       .arg(l.x1())
                       .arg(l.y1())
                       .arg(l.x2())
                       .arg(l.y2())
                       .arg(pen.color().name())
                       .arg(pen.widthF());
        }
        else if (auto* textItem = dynamic_cast<QGraphicsTextItem*>(item))
        {
            const QRectF textSceneRect = textItem->sceneBoundingRect();
            const QFont font = textItem->font();
            const QString weight = font.bold() ? "bold" : "normal";
            // Точный размер шрифта в пикселях, чтобы текст не растягивался в SVG
            const int pixelSize = qRound(font.pointSizeF() * 1.05);
            const qreal baselineY = textSceneRect.top() + textSceneRect.height() * 0.74;

            QGraphicsItem* parentCard = textItem->parentItem();
            QString clipAttr;
            if (parentCard && cardClipIds.contains(parentCard))
            {
                clipAttr = QString(" clip-path=\"url(#cardClip%1)\"").arg(cardClipIds[parentCard]);
            }

            // Если это заголовок карточки (жирный шрифт внутри карточки), центрируем его идеально ровно
            if (parentCard && font.bold())
            {
                const QRectF cardRect = parentCard->sceneBoundingRect();
                out << QString("  <text x=\"%1\" y=\"%2\" text-anchor=\"middle\" font-family=\"Arial, Helvetica, sans-serif\" font-size=\"%3px\" font-weight=\"bold\" fill=\"%4\"%5>%6</text>\n")
                           .arg(cardRect.center().x())
                           .arg(baselineY)
                           .arg(pixelSize)
                           .arg(textItem->defaultTextColor().name())
                           .arg(clipAttr)
                           .arg(escapeXml(textItem->toPlainText()));
            }
            else
            {
                out << QString("  <text x=\"%1\" y=\"%2\" font-family=\"Arial, Helvetica, sans-serif\" font-size=\"%3px\" font-weight=\"%4\" fill=\"%5\"%6>%7</text>\n")
                .arg(textSceneRect.left() + 4.0)
                    .arg(baselineY)
                    .arg(pixelSize)
                    .arg(weight)
                    .arg(textItem->defaultTextColor().name())
                    .arg(clipAttr)
                    .arg(escapeXml(textItem->toPlainText()));
            }
        }
    }

    out << "</svg>\n";
    file.close();
    return true;
}