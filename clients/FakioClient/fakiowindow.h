#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMenu>
#include <QMainWindow>
#include <QProcess>
#include <QSystemTrayIcon>

#include "fakioconfig.h"

namespace Ui {
class FakioWindow;
}

class FakioWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit FakioWindow(QWidget *parent = 0);
    ~FakioWindow();

protected:
    void closeEvent(QCloseEvent *event);

private:
    inline bool fakioExists(QString file);
    inline void startFclient();
    inline void stopFclient();
    void setupTrayicon();
    void setupSettings();
    void setupAbout();

private slots:
    void on_StartButton_clicked();
    void on_tabMain_tabBarClicked(int index);
    void on_saveButton_clicked();
    void on_cancelButton_clicked();
    void on_aboutBrowser_anchorClicked(const QUrl &arg1);

    //custom slots
    void fclient_readReady();
    void trayicon_Clicked(QSystemTrayIcon::ActivationReason reason);
    void trayicon_quitAction_triggered();

private:
    Ui::FakioWindow *ui;
    QSystemTrayIcon *trayicon;
    QMenu *trayiconMenu;
    QAction *openAction;
    QAction *quitAction;

    bool isFclientStart;
    QProcess *fclient;
    QString fclientStatus;
    FakioConfig config;
};

#endif // MAINWINDOW_H
