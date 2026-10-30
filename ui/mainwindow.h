#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "../diagramview.h"
#include "uml_diagram.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onOpenFileClicked();
    void onExportPngClicked();
    void onExportSvgClicked();
    void onResetViewClicked();

    // Новый слот для полной очистки сцены
    void onClearSceneClicked();

private:
    Ui::MainWindow *ui;
    DiagramView *diagramView;

    UmlDiagram createExampleDiagram();
};

#endif // MAINWINDOW_H