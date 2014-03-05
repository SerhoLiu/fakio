#include "fakiowindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    FakioWindow w;
    w.show();

    return a.exec();
}
