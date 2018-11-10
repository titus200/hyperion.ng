//hyperion
#include "LedDevicePhilipsHueEntertainment.h"

// Qt includes
#include <QDebug>

// Mbedtls
#include "mbedtls/config.h"
#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"
#include "mbedtls/timing.h"

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

HueEntertainmentWorker::HueEntertainmentWorker(QString output, QString username, QString clientkey, std::vector<PhilipsHueLight>* lights): output(output), username(username), clientkey(clientkey), lights(lights) {
}

void HueEntertainmentWorker::run() {

#define READ_TIMEOUT_MS 1000
#define MAX_RETRY       5
#define DEBUG_LEVEL 2000
#define SERVER_PORT "2100"
#define SERVER_NAME "Hue"

    int ret;
    mbedtls_net_context server_fd;
    const char *pers = "dtls_client";
    int retry_left = MAX_RETRY;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_timing_delay_context timer;

    //mbedtls_debug_set_threshold(1000);
    mbedtls_debug_set_threshold(4);

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
    
    qDebug() << "Seeding the random number generator...";

    mbedtls_entropy_init(&entropy);

    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers))) != 0) {
        mbedtls_printf(" failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret);
        goto exit;
    }

    /*
    * 1. Start the connection
    */
    
    qDebug() << "Connecting to udp" << output << SERVER_PORT;

    if ((ret = mbedtls_net_connect(&server_fd, output.toUtf8(), SERVER_PORT, MBEDTLS_NET_PROTO_UDP)) != 0) {
        qCritical() << "mbedtls_net_connect FAILED" << ret;
        goto exit;
    }

    /*
    * 2. Setup stuff
    */

    qDebug() << "Setting up the DTLS structure...";

    if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        qCritical() << "mbedtls_ssl_config_defaults FAILED" << ret;
        goto exit;
    }

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
        qCritical() << "mbedtls_ssl_setup FAILED" << ret;
        goto exit;
    }

    if (0 != (ret = mbedtls_ssl_conf_psk(&conf, (const unsigned char*)pskRawArray.data(), pskRawArray.length() * sizeof(char), (const unsigned char *)pskIdRawArray.data(), pskIdRawArray.length() * sizeof(char)))) {
        qCritical() << "mbedtls_ssl_conf_psk FAILED" << ret;
    }

    int ciphers[2];
    ciphers[0] = MBEDTLS_TLS_PSK_WITH_AES_128_GCM_SHA256;
    ciphers[1] = 0;
    mbedtls_ssl_conf_ciphersuites(&conf, ciphers);

    if ((ret = mbedtls_ssl_set_hostname(&ssl, SERVER_NAME)) != 0) {
        qCritical() << "mbedtls_ssl_set_hostname FAILED" << ret;
        goto exit;
    }

    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
    mbedtls_ssl_set_timer_cb(&ssl, &timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);

    /*
    * 4. Handshake
    */

    qDebug() << "Performing the DTLS handshake...";

    for (int attempt = 0; attempt < 4; ++attempt) {
        qDebug() << "handshake attempt" << attempt;
        mbedtls_ssl_conf_handshake_timeout(&conf, 400, 1000);
        do ret = mbedtls_ssl_handshake(&ssl);
        while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

        if (ret == 0)
            break;

        msleep(200);
    }

    qDebug() << "handshake result" << ret;

    if (ret != 0) {
        qCritical() << "mbedtls_ssl_handshake FAILED" << ret;
        goto exit;
    }

    qDebug() << "Handshake successful. Connected!";


    /*
    * 6. Send messages repeatedly until we lose connection or are told to stop
    */
send_request:
    /*
    char header[] = {
            'H', 'u', 'e', 'S', 't', 'r', 'e', 'a', 'm', //protocol
            0x01, 0x00, //version 1.0
            0x01, //sequence number 1
            0x00, 0x00, //reserved
            0x01, //color mode XY
            0x00, //linear filter
    };
    */
    while (true) {
        static const uint8_t HEADER[] = {
            'H', 'u', 'e', 'S', 't', 'r', 'e', 'a', 'm', //protocol
            0x01, 0x00, //version 1.0
            0x01, //sequence number 1
            0x00, 0x00, //Reserved write 0’s
            0x01,
            0x00, // Reserved, write 0’s
        };

        static const uint8_t PAYLOAD_PER_LIGHT[] = {
            0x01, 0x00, 0x06, //light ID
            //color: 16 bpc
            0xff, 0xff,
            0xff, 0xff,
            0xff, 0xff,
            /*
            (message.R >> 8) & 0xff, message.R & 0xff,
            (message.G >> 8) & 0xff, message.G & 0xff,
            (message.B >> 8) & 0xff, message.B & 0xff
            */
        };

        QByteArray Msg;

        Msg.reserve(sizeof(HEADER) + sizeof(PAYLOAD_PER_LIGHT) *  PhilipsHueLight& lamp.size());
        Msg.append((char*)HEADER, sizeof(HEADER));

        //unsigned int idx = 0;
        for (const PhilipsHueLight& lamp : *lights) {
            quint64 R = lamp.getColor().x * 0xffff;
            quint64 G = lamp.getColor().y * 0xffff;
            quint64 B = lamp.getColor().bri * 0xffff;

            const uint8_t payload[] = {
                0x00, 0x00, ((uint8_t)lamp.id.toInt()),
                static_cast<uint8_t>((R >> 8) & 0xff), static_cast<uint8_t>(R & 0xff),
                static_cast<uint8_t>((G >> 8) & 0xff), static_cast<uint8_t>(G & 0xff),
                static_cast<uint8_t>((B >> 8) & 0xff), static_cast<uint8_t>(B & 0xff)
            };

            Msg.append((char*)payload, sizeof(payload));
        }

        int len = Msg.size();

        do ret = mbedtls_ssl_write(&ssl, (unsigned char *)Msg.data(), len);
        while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

        if(ret < 0) {
            break;
        }
        QThread::msleep(30);
    }

    if (ret < 0) {
        switch (ret) {
            case MBEDTLS_ERR_SSL_TIMEOUT:
                qWarning() << " timeout";
                if (retry_left-- > 0)
                    goto send_request;
                goto exit;
            case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
                qWarning() << " connection was closed gracefully";
                ret = 0;
                goto close_notify;
            default:
                qWarning() << " mbedtls_ssl_read returned" << ret;
                goto exit;
        }
    }

    /*
    * 8. Done, cleanly close the connection
    */
close_notify:
    qDebug() << "Closing the connection...";

    /* No error checking, the connection might be closed already */
    do ret = mbedtls_ssl_close_notify(&ssl);
    while (ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    ret = 0;

    qDebug() << "Done";

    /*
    * 9. Final clean-ups and exit
    */
exit:

#ifdef MBEDTLS_ERROR_C
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, 100);
        mbedtls_printf("Last error was: %d - %s\n\n", ret, error_buf);
    }
#endif

    mbedtls_net_free(&server_fd);

    mbedtls_x509_crt_free(&cacert);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
}
