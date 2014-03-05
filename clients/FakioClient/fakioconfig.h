#ifndef FAKIOCONFIG_H
#define FAKIOCONFIG_H

#include <QJsonValue>
#include <QJsonValueRef>
#include <QJsonObject>


class FakioConfig
{
public:
    FakioConfig();
    bool read(QString configPath);
    bool save();

    QJsonValue operator[](const QString & key) const {
        return fakioConfig[key];
    }

    QJsonValueRef operator[](const QString & key) {
        return fakioConfig[key];
    }


public:
    QJsonObject fakioConfig;

private:
    QString configPath;

};

#endif // FAKIOCONFIG_H
