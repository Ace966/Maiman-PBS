#include "MainWindow.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Maiman PBS");
    resize(900, 600);

    auto *centralWidget = new QWidget(this);
    auto *layout = new QVBoxLayout(centralWidget);

    auto *label = new QLabel("Initial Qt/CMake setup works.", centralWidget);
    layout->addWidget(label);

    setCentralWidget(centralWidget);
}