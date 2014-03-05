#include "fakiowindow.h"
#include "ui_fakiowindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMessageBox>
#include <QScrollBar>
#include <QDebug>

#ifdef Q_OS_WIN
    #include <windows.h>
    #define FCLIENT_FILE "fclient.exe"
#else
    #define FCLIENT_FILE "fclient"
#endif
#define CONFIG_FILE "config.json"
#define ABOUT_FILE "about.txt"

FakioWindow::FakioWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::FakioWindow),
    isFclientStart(false),
    fclient(new QProcess)
{
    ui->setupUi(this);
    ui->aboutBrowser->setOpenLinks(false);

    fclient->setWorkingDirectory(QApplication::applicationDirPath());
    connect(fclient, SIGNAL(readyRead()), this, SLOT(fclient_readReady()));

    if (!fakioExists(FCLIENT_FILE) || !fakioExists(CONFIG_FILE)) {
        QString info = tr("缺少文件: ") + tr(FCLIENT_FILE) +
                       tr(",") + tr(CONFIG_FILE);
        QMessageBox::warning(this, tr("警告"), info);
    }

    setupTrayicon();
    setupSettings();
    setupAbout();
}

FakioWindow::~FakioWindow()
{
    fclient->terminate();
    fclient->waitForFinished();
    delete quitAction;
    delete openAction;
    delete trayiconMenu;
    delete trayicon;
    delete fclient;
    delete ui;
}

bool FakioWindow::fakioExists(QString fileName)
{
    QString workDir = QApplication::applicationDirPath();
    QString filePath = QDir(workDir).filePath(fileName);
    QFile file(filePath);
    return file.exists();
}

void FakioWindow::startFclient()
{
    if (fakioExists(FCLIENT_FILE) && fakioExists(CONFIG_FILE)) {
        fclient->start(FCLIENT_FILE);
        fclientStatus = tr("正在启动...\n");
        isFclientStart = true;
        ui->StartButton->setText("关闭");
    } else {
        fclientStatus = tr("启动失败: 缺少文件 ") + tr(FCLIENT_FILE) +
                        tr(",") + tr(CONFIG_FILE);
    }
    ui->plainStatus->setPlainText(fclientStatus);
}

void FakioWindow::stopFclient()
{
#ifdef Q_OS_WIN
        ::TerminateProcess(fclient->pid()->hProcess, 0);
#else
        fclient->kill();
#endif // Q_OS_WIN
        isFclientStart = false;
        fclientStatus += tr("关闭成功...\n");
        ui->plainStatus->setPlainText(fclientStatus);
        ui->plainStatus->verticalScrollBar()->setValue(
                    ui->plainStatus->verticalScrollBar()->maximumHeight());
        ui->StartButton->setText("启动");
}

void FakioWindow::setupTrayicon()
{
    trayicon = new QSystemTrayIcon(this);
    QIcon icon(":/fakio/resources/tray.ico");
    trayicon->setIcon(icon);

    trayiconMenu = new QMenu(this);
    openAction = new QAction(tr("打开窗口"), this);
    connect(openAction, SIGNAL(triggered()), this, SLOT(show()));
    trayiconMenu->addAction(openAction);
    quitAction = new QAction(tr("退出程序"), this);
    connect(quitAction, SIGNAL(triggered()), this,
            SLOT(trayicon_quitAction_triggered()));
    trayiconMenu->addAction(quitAction);
    trayicon->setContextMenu(trayiconMenu);

    connect(trayicon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayicon_Clicked(QSystemTrayIcon::ActivationReason)));
    trayicon->setVisible(true);
}

void FakioWindow::setupSettings()
{
    QString configPath = QDir(QApplication::applicationDirPath()).filePath(CONFIG_FILE);
    if(!config.read(configPath)) {
        QMessageBox::warning(this, tr("警告"), tr("读取配置文件出错: ")+configPath);
        return;
    }

    ui->fakioEdit->setText(config["server"].toString());
    ui->localEdit->setText(config["local"].toString());
    ui->usernameEdit->setText(config["username"].toString());
    ui->passwordEdit->setText(config["password"].toString());
}

void FakioWindow::setupAbout()
{
    QString aboutPath = QDir(QApplication::applicationDirPath()).filePath(ABOUT_FILE);
    QFile file(aboutPath);
    QString about;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        about = tr("Fakio 客户端: 更多请查看 <a href=\"https://github.com/SerhoLiu/fakio\"> Fakio on Github </a>");
    } else {
        about = file.readAll();
    }
    ui->aboutBrowser->setText(about);
}

void FakioWindow::on_StartButton_clicked()
{
    if (!isFclientStart) {
        ui->plainStatus->clear();
        startFclient();
    } else {
        stopFclient();
    }
}

void FakioWindow::fclient_readReady()
{
    fclientStatus += fclient->readAll();
    ui->plainStatus->setPlainText(fclientStatus);
    if (fclientStatus.length() >= 1024 * 1024 * 5) {
        fclientStatus.clear();
    }
}

void FakioWindow::trayicon_Clicked(QSystemTrayIcon::ActivationReason reason)
{
    switch(reason) {
      case QSystemTrayIcon::Trigger:
      case QSystemTrayIcon::DoubleClick:
          this->setWindowState(Qt::WindowActive);
          this->show();
          break;
      default:
          break;
    }
}

void FakioWindow::trayicon_quitAction_triggered()
{
    if (isFclientStart) {
        stopFclient();
    }
    qApp->quit();
}

void FakioWindow::on_tabMain_tabBarClicked(int index)
{
    if (index == 1) {
        setupSettings();
        return;
    }

    if (index == 2) {
        setupAbout();
        return;
    }
}

void FakioWindow::on_saveButton_clicked()
{
    QString configPath = QDir(QApplication::applicationDirPath()).filePath(CONFIG_FILE);

    config["server"] = ui->fakioEdit->text();
    config["local"] = ui->localEdit->text();
    config["username"] = ui->usernameEdit->text();
    config["password"] = ui->passwordEdit->text();

    if (!config.save()) {
        QMessageBox::warning(this, tr("警告"), tr("保存配置文件出错: ")+configPath);
    } else {
        QMessageBox::information(this, tr("修改成功"), tr("配置修改成功"));
    }
}

void FakioWindow::on_cancelButton_clicked()
{
    on_tabMain_tabBarClicked(1);
}

void FakioWindow::closeEvent(QCloseEvent *event)
{
    if (this->trayicon->isVisible()) {
        event->ignore();
        hide();
        trayicon->showMessage(tr("提示"),tr("Fakio 客户端后台运行中"),
                              QSystemTrayIcon::Information, 5000);
    }
}

void FakioWindow::on_aboutBrowser_anchorClicked(const QUrl &arg1)
{
    QDesktopServices::openUrl(QUrl(arg1));
}
