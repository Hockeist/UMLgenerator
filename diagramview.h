#pragma once

#include <QGraphicsItem>
#include <QGraphicsView>
#include <QMap>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QVector>

#include "uml_diagram.h"

class QWheelEvent;
class QMouseEvent;

class DiagramView : public QGraphicsView
{
public:
    explicit DiagramView(QWidget *parent = nullptr);

    // Загружает и рисует диаграмму
    void setDiagram(const UmlDiagram& diagram);

    // Полностью очищает сцену от всех классов и стрелок
    void clearDiagram();

    // Перестраивает маршруты связей при перетаскивании блоков
    void rebuildRelationships();

    // Экспорт в растровый PNG и векторный SVG форматы
    bool exportToPng(const QString& filePath);
    bool exportToSvg(const QString& filePath);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void drawDiagram();
    void clearRelationshipItems();

    UmlDiagram m_diagram;
    QMap<QString, QGraphicsItem*> m_classItems;
    QVector<QGraphicsItem*> m_relationshipItems;

    bool m_isPanning = false;
    QPoint m_lastPanPoint;
};