#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "../parser/cpp_parser.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , diagramView(new DiagramView(this))
{
    ui->setupUi(this);
    setWindowTitle("UML Generator — Анализатор кода (C++ / Python / C#)");
    resize(1180, 780);

    QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout*>(ui->centralwidget->layout());
    if (mainLayout == nullptr)
    {
        mainLayout = new QVBoxLayout(ui->centralwidget);
    }

    // 1. Верхняя панель управления с новой кнопкой «Очистить сцену»
    auto *toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(10);

    auto *openButton = new QPushButton("Открыть код (C++ / Python / C#)...", ui->centralwidget);
    auto *exportPngButton = new QPushButton("Экспорт в PNG...", ui->centralwidget);
    auto *exportSvgButton = new QPushButton("Экспорт в SVG...", ui->centralwidget);
    auto *resetViewButton = new QPushButton("Вписать в окно", ui->centralwidget);
    auto *clearSceneButton = new QPushButton("Очистить сцену", ui->centralwidget);

    auto *hintLabel = new QLabel(
        "Управление: мышь на фоне — сдвиг камеры, на блоке — сдвиг класса, колесико — зум",
        ui->centralwidget
        );
    hintLabel->setStyleSheet("color: #667085; font-size: 12px;");

    toolbarLayout->addWidget(openButton);
    toolbarLayout->addWidget(exportPngButton);
    toolbarLayout->addWidget(exportSvgButton);
    toolbarLayout->addWidget(resetViewButton);
    toolbarLayout->addWidget(clearSceneButton);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(hintLabel);

    mainLayout->addLayout(toolbarLayout);
    mainLayout->addWidget(diagramView, 1);

    // Показываем демонстрационную диаграмму при старте
    UmlDiagram diagram = createExampleDiagram();
    diagramView->setDiagram(diagram);
    statusBar()->showMessage(
        "Загружена демонстрационная UML-диаграмма. Нажмите «Открыть код...» для анализа файла."
        );

    // Подключаем кнопки к слотам
    connect(openButton, &QPushButton::clicked, this, &MainWindow::onOpenFileClicked);
    connect(exportPngButton, &QPushButton::clicked, this, &MainWindow::onExportPngClicked);
    connect(exportSvgButton, &QPushButton::clicked, this, &MainWindow::onExportSvgClicked);
    connect(resetViewButton, &QPushButton::clicked, this, &MainWindow::onResetViewClicked);
    connect(clearSceneButton, &QPushButton::clicked, this, &MainWindow::onClearSceneClicked);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onOpenFileClicked()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        "Выберите исходный файл (C++, Python, C#)",
        QString(),
        "Все поддерживаемые файлы (*.cpp *.h *.hpp *.cc *.cxx *.py *.cs);;"
        "Файлы C++ (*.cpp *.h *.hpp *.cc *.cxx);;"
        "Файлы Python (*.py);;"
        "Файлы C# (*.cs);;"
        "Все файлы (*)"
        );

    if (filePath.isEmpty())
        return;

    CppParser parser;
    UmlDiagram parsedDiagram;
    std::string errorMessage;

    const bool success = parser.parseFile(
        filePath.toStdString(),
        parsedDiagram,
        errorMessage
        );

    if (!success && parsedDiagram.classes().empty())
    {
        QMessageBox::warning(
            this,
            "Ошибка разбора файла",
            QString::fromStdString(errorMessage)
            );
        return;
    }

    if (parsedDiagram.classes().empty())
    {
        QMessageBox::information(
            this,
            "Классы не найдены",
            "В выбранном файле не обнаружено объявлений пользовательских классов или структур."
            );
        return;
    }

    diagramView->setDiagram(parsedDiagram);

    const QString ext = QFileInfo(filePath).suffix().toLower();
    QString langName = "C++";
    if (ext == "py")
        langName = "Python";
    else if (ext == "cs")
        langName = "C#";

    statusBar()->showMessage(
        QString("Язык: %1 | Файл: %2 | Найдено классов: %3 | Связей: %4")
            .arg(langName)
            .arg(QFileInfo(filePath).fileName())
            .arg(parsedDiagram.classes().size())
            .arg(parsedDiagram.relationships().size())
        );

    if (!errorMessage.empty())
    {
        QMessageBox::information(
            this,
            "Разбор завершен с предупреждениями",
            QString("Диаграмма построена (классов: %1), но анализатор сообщил о предупреждениях:\n\n%2")
                .arg(parsedDiagram.classes().size())
                .arg(QString::fromStdString(errorMessage))
            );
    }
}

void MainWindow::onExportPngClicked()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        "Сохранить диаграмму в PNG",
        "uml_diagram.png",
        "PNG Image (*.png)"
        );

    if (filePath.isEmpty())
        return;

    if (diagramView->exportToPng(filePath))
    {
        statusBar()->showMessage("Диаграмма успешно сохранена в PNG: " + filePath);
        QMessageBox::information(this, "Экспорт завершен", "Изображение PNG успешно сохранено!");
    }
    else
    {
        QMessageBox::warning(this, "Ошибка экспорта", "Не удалось сохранить файл PNG (возможно, сцена пуста).");
    }
}

void MainWindow::onExportSvgClicked()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        "Сохранить диаграмму в SVG",
        "uml_diagram.svg",
        "SVG Vector Image (*.svg)"
        );

    if (filePath.isEmpty())
        return;

    if (diagramView->exportToSvg(filePath))
    {
        statusBar()->showMessage("Диаграмма успешно сохранена в SVG: " + filePath);
        QMessageBox::information(this, "Экспорт завершен", "Векторный файл SVG успешно сохранен!");
    }
    else
    {
        QMessageBox::warning(this, "Ошибка экспорта", "Не удалось сохранить файл SVG (возможно, сцена пуста).");
    }
}

void MainWindow::onResetViewClicked()
{
    if (diagramView->scene() && !diagramView->scene()->items().isEmpty())
    {
        diagramView->resetTransform();
        const QRectF contentRect =
            diagramView->scene()->itemsBoundingRect().adjusted(-40.0, -40.0, 40.0, 40.0);

        diagramView->fitInView(contentRect, Qt::KeepAspectRatio);
        if (diagramView->transform().m11() > 1.0)
            diagramView->resetTransform();

        diagramView->centerOn(contentRect.center());
    }
}

// Обработчик кнопки «Очистить сцену»
void MainWindow::onClearSceneClicked()
{
    diagramView->clearDiagram();
    statusBar()->showMessage("Сцена очищена. Нажмите «Открыть код...», чтобы построить новую диаграмму.");
}

UmlDiagram MainWindow::createExampleDiagram()
{
    UmlDiagram diagram;

    UmlClass customer("Customer");
    customer.addAttribute("name: String", UmlAccess::Private);
    customer.addAttribute("address: String", UmlAccess::Private);

    UmlClass order("Order");
    order.addAttribute("date: Date", UmlAccess::Private);
    order.addAttribute("status: String", UmlAccess::Private);
    order.addMethod("calcSubTotal(): float", UmlAccess::Public);
    order.addMethod("calcTax(): float", UmlAccess::Public);
    order.addMethod("calcTotal(): float", UmlAccess::Public);
    order.addMethod("calcTotalWeight(): float", UmlAccess::Public);

    UmlClass orderDetail("OrderDetail");
    orderDetail.addAttribute("quantity: int", UmlAccess::Private);
    orderDetail.addAttribute("taxStatus: String", UmlAccess::Private);
    orderDetail.addMethod("calcSubTotal(): float", UmlAccess::Public);
    orderDetail.addMethod("calcWeight(): float", UmlAccess::Public);
    orderDetail.addMethod("calcTax(): float", UmlAccess::Public);

    UmlClass item("Item");
    item.addAttribute("shippingWeight: float", UmlAccess::Private);
    item.addAttribute("description: String", UmlAccess::Private);
    item.addMethod("getPriceForQuantity(): float", UmlAccess::Public);
    item.addMethod("getTax(): float", UmlAccess::Public);
    item.addMethod("inStock(): bool", UmlAccess::Public);

    UmlClass payment("Payment");
    payment.addAttribute("amount: float", UmlAccess::Private);

    UmlClass cash("Cash");
    cash.addAttribute("cashTendered: float", UmlAccess::Private);

    UmlClass check("Check");
    check.addAttribute("name: String", UmlAccess::Private);
    check.addAttribute("bankID: String", UmlAccess::Private);
    check.addMethod("authorized(): bool", UmlAccess::Public);

    UmlClass credit("Credit");
    credit.addAttribute("name: String", UmlAccess::Private);
    credit.addAttribute("type: String", UmlAccess::Private);
    credit.addAttribute("expDate: Date", UmlAccess::Private);
    credit.addMethod("authorized(): bool", UmlAccess::Public);

    diagram.addClass(customer);
    diagram.addClass(order);
    diagram.addClass(orderDetail);
    diagram.addClass(item);
    diagram.addClass(payment);
    diagram.addClass(cash);
    diagram.addClass(check);
    diagram.addClass(credit);

    diagram.addRelationship(UmlRelationship("Order", "Customer", RelationshipType::Association, "0..*", "1"));
    diagram.addRelationship(UmlRelationship("Order", "OrderDetail", RelationshipType::Aggregation, "1", "1..*"));
    diagram.addRelationship(UmlRelationship("OrderDetail", "Item", RelationshipType::Association, "0..*", "1"));
    diagram.addRelationship(UmlRelationship("Order", "Payment", RelationshipType::Association, "1", "1..*"));

    diagram.addRelationship(UmlRelationship("Cash", "Payment", RelationshipType::Inheritance));
    diagram.addRelationship(UmlRelationship("Check", "Payment", RelationshipType::Inheritance));
    diagram.addRelationship(UmlRelationship("Credit", "Payment", RelationshipType::Inheritance));

    return diagram;
}