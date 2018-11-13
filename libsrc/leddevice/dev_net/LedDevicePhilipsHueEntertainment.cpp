#include "LedDevicePhilipsHueEntertainment.h"

// Qt includes
#include <QDebug>

// Mbedtls
#include "mbedtls/net_sockets.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"
#include "mbedtls/timing.h"
#include "mbedtls/config.h"

LedDevice* LedDevicePhilipsHueEntertainment::construct(const QJsonObject &deviceConfig)
{
    return new LedDevicePhilipsHueEntertainment(deviceConfig);
}

LedDevicePhilipsHueEntertainment::LedDevicePhilipsHueEntertainment(const QJsonObject& deviceConfig)
        : LedDevice()
        , bridge(_log, deviceConfig["output"].toString(), deviceConfig["username"].toString())
{
    _deviceReady = init(deviceConfig);

    connect(&bridge, &PhilipsHueBridge::newGroups, this, &LedDevicePhilipsHueEntertainment::newGroups);
    connect(&bridge, &PhilipsHueBridge::newLights, this, &LedDevicePhilipsHueEntertainment::newLights);
    connect(this, &LedDevice::enableStateChanged, this, &LedDevicePhilipsHueEntertainment::stateChanged);


    worker = new HueEntertainmentWorker(deviceConfig["output"].toString(), deviceConfig["username"].toString(), deviceConfig["clientkey"].toString(), &lights);
}

bool LedDevicePhilipsHueEntertainment::init(const QJsonObject &deviceConfig)
{
    groupId = deviceConfig["groupId"].toInt();

    // get light info from bridge
    bridge.bConnect();

    LedDevice::init(deviceConfig);

    return true;
}

LedDevicePhilipsHueEntertainment::~LedDevicePhilipsHueEntertainment() {
    worker->terminate();
    worker->wait();
    delete worker;
    switchOff();
}

void LedDevicePhilipsHueEntertainment::newGroups(QMap<quint16, QJsonObject> map)
{
        // search user groupid inside map and create light if found
        if(map.contains(groupId))
        {
            QJsonObject group = map.value(groupId);

            if(group.value("type") == "Entertainment")
            {
                QJsonArray jsonLights = group.value("lights").toArray();
                for(const auto id: jsonLights)
                {
                    lightIds.push_back(id.toString().toInt());
                }
            }
            else
            {
                Error(_log,"Group id %d is not an entertainment group", groupId);
            }
        }
        else
        {
            Error(_log,"Group id %d isn't used on this bridge", groupId);
        }
}

void LedDevicePhilipsHueEntertainment::newLights(QMap<quint16, QJsonObject> map)
{
    if(!lightIds.empty())
    {
        // search user lightid inside map and create light if found
        lights.clear();
        for(const auto id : lightIds)
        {
            if (map.contains(id))
            {
                lights.push_back(PhilipsHueLight(_log, bridge, id, map.value(id)));
            }
            else
            {
                Error(_log,"Light id %d isn't used on this bridge", id);
            }
        }

        bridge.post(QString("groups/%1").arg(groupId), "{\"stream\":{\"active\":true}}");
        worker->start();
    }
}

int LedDevicePhilipsHueEntertainment::write(const std::vector <ColorRgb> &ledValues) {
    // lights will be empty sometimes
    if(lights.empty()) return -1;

    unsigned int idx = 0;
    for (const ColorRgb& color : ledValues) {
        // Get lamp.
        PhilipsHueLight& lamp = lights.at(idx);
        // Scale colors from [0, 255] to [0, 1] and convert to xy space.
        CiColor xy = CiColor::rgbToCiColor(color.red / 255.0f, color.green / 255.0f, color.blue / 255.0f, lamp.getColorSpace());

        if(xy != lamp.getColor()) {
            // Remember last color.
            lamp.setColor(xy, 1.0);
        }

        // Next light id.
        idx++;
    }
    return 0;
}

void LedDevicePhilipsHueEntertainment::stateChanged(bool newState)
{
    if(newState)
        bridge.bConnect();
    else
        lights.clear();
}

HueEntertainmentWorker::HueEntertainmentWorker(QString output, QString username, QString clientkey, std::vector<PhilipsHueLight>* lights): output(output),
                                                                                      username(username),
                                                                                      clientkey(clientkey),
                                                                                      lights(lights) {
}

void HueEntertainmentWorker::run() {
    int ret;
    const char *pers = "dtls_client";

    mbedtls_net_context server_fd;
    mbedtls_ssl_context ssl;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_timing_delay_context timer;

    mbedtls_debug_set_threshold(3);

    /*
    * -1. Load psk
    */
    QByteArray pskArray = clientkey.toUtf8();
    QByteArray pskRawArray = QByteArray::fromHex(pskArray);


    QByteArray pskIdArray = username.toUtf8();
    QByteArray pskIdRawArray = pskIdArray;

    /*
    * 0. Initialize the RNG and the session data
    */
    mbedtls_net_init(&server_fd);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    mbedtls_entropy_init(&entropy);
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const unsigned char *)pers,
                                     strlen(pers))) != 0)
    {
        qFatal("mbedtls_ctr_drbg_seed returned %d", ret);
    }

    /*
* 1. Start the connection
*/
    if ((ret = mbedtls_net_connect(&server_fd, output.toUtf8(),
                                   "2100", MBEDTLS_NET_PROTO_UDP)) != 0)
    {
        qFatal("mbedtls_net_connect FAILED %d", ret);
    }


    /*
 * 2. Setup stuff
 */
    if ((ret = mbedtls_ssl_config_defaults(&conf,
                                           MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
    {
        qFatal("mbedtls_ssl_config_defaults FAILED %d", ret);
    }

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0)
    {
        qFatal("mbedtls_ssl_setup FAILED %d", ret);
    }

    if (0 != (ret = mbedtls_ssl_conf_psk(&conf, (const unsigned char*)pskRawArray.data(), pskRawArray.length() * sizeof(char),
                                         (const unsigned char *)pskIdRawArray.data(), pskIdRawArray.length() * sizeof(char))))
    {
        qFatal("mbedtls_ssl_conf_psk FAILED %d", ret);
    }

    int ciphers[2];
    ciphers[0] = MBEDTLS_TLS_PSK_WITH_AES_128_GCM_SHA256;
    ciphers[1] = 0;
    mbedtls_ssl_conf_ciphersuites(&conf, ciphers);

    if ((ret = mbedtls_ssl_set_hostname(&ssl, "Hue")) != 0)
    {
        qCritical() << "mbedtls_ssl_set_hostname FAILED" << ret;
    }

    mbedtls_ssl_set_bio(&ssl, &server_fd,
                        mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);

    mbedtls_ssl_set_timer_cb(&ssl, &timer, mbedtls_timing_set_delay,
                             mbedtls_timing_get_delay);

    /*
 * 4. Handshake
 */
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        mbedtls_ssl_conf_handshake_timeout(&conf, 400, 1000);
        do ret = mbedtls_ssl_handshake(&ssl);
        while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
               ret == MBEDTLS_ERR_SSL_WANT_WRITE);

        if (ret == 0)
            break;
    }

    if (ret != 0)
    {
        qFatal() << "mbedtls_ssl_handshake returned -0x%x\n\n" << -ret;
    }

    char header[] = {
            'H', 'u', 'e', 'S', 't', 'r', 'e', 'a', 'm', //protocol
            0x01, 0x00, //version 1.0
            0x01, //sequence number 1
            0x00, 0x00, //reserved
            0x01, //color mode XY
            0x00, //linear filter
    };

    while (true)
    {
        QByteArray Msg;

        Msg.append(header, sizeof(header));

        unsigned int idx = 0;
        for (const PhilipsHueLight& lamp : *lights) {
            quint64 R = lamp.getColor().x * 0xffff;
            quint64 G = lamp.getColor().y * 0xffff;
            quint64 B = lamp.getColor().bri * 0xffff;

            unsigned int id = lamp.getId();
            char light_stream[] = {
                    0x00, 0x00, (char)id, //light ID
                    static_cast<uint8_t>((R >> 8) & 0xff), static_cast<uint8_t>(R & 0xff),
                    static_cast<uint8_t>((G >> 8) & 0xff), static_cast<uint8_t>(G & 0xff),
                    static_cast<uint8_t>((B >> 8) & 0xff), static_cast<uint8_t>(B & 0xff)
            };

            Msg.append(light_stream, sizeof(light_stream));
            idx++;
        }

        int len = Msg.size();

        do ret = mbedtls_ssl_write(&ssl,  (unsigned char *) Msg.data(), len);
        while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
               ret == MBEDTLS_ERR_SSL_WANT_WRITE);

        if(ret < 0) {
            break;
        }
        QThread::msleep(40);
    }

    mbedtls_net_free(&server_fd);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
}