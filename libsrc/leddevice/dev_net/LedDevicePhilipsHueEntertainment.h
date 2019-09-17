#pragma once

#include "LedDevicePhilipsHue.h"

// Qt includes
#include <QMutex>
#include <QMutexLocker>

#include <chrono>

//----------- mbedtls

#if !defined(MBEDTLS_CONFIG_FILE)
#include <mbedtls/config.h>
#else
#include MBEDTLS_CONFIG_FILE
#endif

#if defined(MBEDTLS_PLATFORM_C)
#include <mbedtls/platform.h>
#else
#include <stdio.h>
#define mbedtls_printf     printf
#define mbedtls_fprintf    fprintf
#endif

#include <string.h>

//#include "mbedtls/certs.h"
#include <mbedtls/net_sockets.h>
//#include <mbedtls/ssl.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <mbedtls/entropy.h>
#include <mbedtls/timing.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/debug.h>

#define READ_TIMEOUT_MS 1000
#define MAX_RETRY       5
//#define DEBUG_LEVEL 0
#define SERVER_PORT "2100"
#define SERVER_NAME "Hue"

//----------- END mbedtls

class HueEntertainmentStreamer : public QObject
{
	Q_OBJECT

public:

	explicit HueEntertainmentStreamer(Logger* log, std::vector<PhilipsHueLight>* lights, const QJsonObject &deviceConfig);
	~HueEntertainmentStreamer();

	void checkStreamGroupState();
	//void prepareStreamGroup();
	void startStreamer();
	void stopStreamer();
	void streamToBridge();
	bool _isRunning;
	bool _hasBridgeConnection;
	bool _isStreaming;

public slots:
	void buildStreamer();

private:

	Logger* _log;
	/// Array to save the lamps.
	std::vector<PhilipsHueLight> *lights;
	QJsonObject _deviceConfig;
	unsigned int _streamFrequency;
	bool _debugStreamer;
	unsigned int _debugLevel;
	bool logCommands;
	QString address;
	QString username;
	QString clientkey;
	unsigned int groupId;
	QString groupname;

	const int ciphers[1] = {MBEDTLS_TLS_PSK_WITH_AES_128_GCM_SHA256};

	bool initStreamer();
	bool seedingRNG();
	bool setupStructure();
	bool startUPDConnection();
	bool setupPSK();
	bool startSSLHandshake();

	mbedtls_net_context client_fd;
	mbedtls_entropy_context entropy;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt cacert;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_timing_delay_context timer;
	const char *pers = "dtls_client";
	int retry_left;

	QMutex _hueMutex;
	CiColor lampC;
	QByteArray prepareMsgData();
	void handleReturn(int ret);

signals:
	void getStreamGroupState(bool startStreaming = true);
	void setStreamGroupState(bool state = false, bool startStreaming = true);

protected:
//#if DEBUG_LEVEL > 0
	static void HueEntertainmentStreamerDebug(void *ctx, int level, const char *file, int line, const char *str)
	{
		const char *p, *basename;
		(void) ctx;
		/* Extract basename from file */
		for(p = basename = file; *p != '\0'; p++)
		{
			if(*p == '/' || *p == '\\')
			{
				basename = p + 1;
			}
		}
		mbedtls_printf("%s:%04d: |%d| %s", basename, line, level, str);
	}
//#endif
};

class LedDevicePhilipsHueEntertainment : public PhilipsHueBridge
{
	Q_OBJECT

public:
	LedDevicePhilipsHueEntertainment(const QJsonObject &deviceConfig);
	~LedDevicePhilipsHueEntertainment();

	/// constructs leddevice
	static LedDevice *construct(const QJsonObject &deviceConfig);

	HueEntertainmentStreamer* _streamer;

	/// Switch the leds on
	virtual int switchOn();

	/// Switch the leds off
	virtual int switchOff();

public slots:
	/// thread start
	virtual void start();

	void restoreOriginalState();

private:
	/// bridge class
	// PhilipsHueBridge* _bridge;
	void initBridge();
	void startStreaming();
	void exitRoutine();
	void cleanupStreamer();

	bool _firstSwitchOn;
	bool _isActive;
	bool _isStreaming;
	bool _prepareSwitchOff;
	bool _prepareStreaming;

	std::vector<unsigned int> lightIds;

	/// The brightness factor to multiply on color change.
	double brightnessFactor;
	/// The min brightness value.
	double brightnessMin;
	/// The max brightness value.
	double brightnessMax;
	/// Logging more Informatons if true
	bool logCommands;
	QJsonObject _deviceConfig;

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

	/// creates new PhilipsHueLight(s) based on user lightid with bridge feedback
	///
	/// @param map Map of lightid/value pairs of bridge
	///
	void updateLights(QMap <quint16, QJsonObject> map);

	void stateChanged(bool newState);

protected:
	/// used GroupID
	unsigned int groupId;
	/// Groupname
	QString groupname = "";
	/// Array to save the lamps.
	std::vector<PhilipsHueLight> lights;

	/// Sends the given led-color values via put request to the hue system
	///
	/// @param ledValues The color-value per led
	///
	/// @return Zero on success else negative
	///
	int write(const std::vector<ColorRgb> &ledValues);

	bool init(const QJsonObject &deviceConfig);
};
