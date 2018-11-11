#pragma once

#include "LedDevicePhilipsHue.h"

// Qt includes
#include <QObject>
#include <QThread>
#include <QJsonObject>
#include <QMutex>
#include <string.h>

// Leddevice includes
#include <leddevice/LedDevice.h>

// Mbedtls
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

class HueEntertainmentWorker : public QThread
{
    Q_OBJECT;

public:
    HueEntertainmentWorker(QString output, QString username, QString clientkey, std::vector <PhilipsHueLight> *lights);

    void run();

private:
    /// Output
    QString output;
    /// Username
    QString username;
    /// Clientkey
    QString clientkey;
    /// GroupId
    unsigned int groupId;
    /// Array to save the lamps.
    std::vector <PhilipsHueLight> *lights;
    QMutex eMutex;
    unsigned char cMsg;
    unsigned char dMsg;
};

class LedDevicePhilipsHueEntertainment : public LedDevice
{
    Q_OBJECT;

private:
    /// bridge class
    PhilipsHueBridge bridge;

public:
    LedDevicePhilipsHueEntertainment(const QJsonObject &deviceConfig);

    virtual ~LedDevicePhilipsHueEntertainment();

    /// constructs leddevice
    static LedDevice *construct(const QJsonObject &deviceConfig);

private slots:
    /// creates new PhilipsHueLight(s) based on user groupid with bridge feedback
    ///
    /// @param map Map of groupid/value pairs of bridge
    ///
    void newGroups(QMap<quint16, QJsonObject> map);

    /// creates new PhilipsHueLight(s) based on user lightid with bridge feedback
    ///
    /// @param map Map of lightid/value pairs of bridge
    ///
    void newLights(QMap <quint16, QJsonObject> map);

    void stateChanged(bool newState);

protected:
    QString username;

    /// Clientkey
    QString clientkey;
    unsigned int groupId;
    /// Array to save the lamps.
    std::vector <PhilipsHueLight> lights;

    Logger *log;
    HueEntertainmentWorker *worker;

    /// Sends the given led-color values via put request to the hue system
    ///
    /// @param ledValues The color-value per led
    ///
    /// @return Zero on success else negative
    ///
    virtual int write(const std::vector <ColorRgb> &ledValues);

    bool init(const QJsonObject &deviceConfig);

    std::vector<unsigned int> lightIds;
};