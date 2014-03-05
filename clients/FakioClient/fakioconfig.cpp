#include "fakioconfig.h"
#include <QFile>
#include <QDebug>
#include <QJsonDocument>

FakioConfig::FakioConfig()
{

}

bool FakioConfig::read(QString configPath)
{
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    this->configPath = configPath;
    QByteArray configJson = file.readAll();
    file.close();

    if (configJson.isEmpty()) {
        return false;
    }

    QJsonDocument jsonDoc = QJsonDocument::fromJson(configJson);
    fakioConfig = jsonDoc.object();
    return true;
}

bool FakioConfig::save()
{
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QByteArray configJson = QJsonDocument(fakioConfig).toJson();
    file.write(configJson);
    file.close();
    return true;
}
