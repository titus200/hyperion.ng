#include "LedDevicePhilipsHueEntertainment.h"

LedDevice* LedDevicePhilipsHueEntertainment::construct(const QJsonObject &deviceConfig)
{
	return new LedDevicePhilipsHueEntertainment(deviceConfig);
}

LedDevicePhilipsHueEntertainment::LedDevicePhilipsHueEntertainment(const QJsonObject &deviceConfig)
	: PhilipsHueBridge(deviceConfig)
	, _streamer(nullptr)
	, _firstSwitchOn(true)
	, _isActive(true)
	, _isStreaming(false)
	, _prepareSwitchOff(false)
	, _prepareStreaming(false)
{

}

void LedDevicePhilipsHueEntertainment::start()
{
	Debug(_log, "Philips Hue Entertainment Thread started");
	_deviceReady = init(_devConfig);
	initBridge();
	//connect(this, &LedDevice::activeStateChanged, this, &LedDevicePhilipsHueEntertainment::stateChanged);
	connect(this, &LedDevice::enableStateChanged, this, &LedDevicePhilipsHueEntertainment::stateChanged);
}

bool LedDevicePhilipsHueEntertainment::init(const QJsonObject &deviceConfig)
{
	brightnessFactor = deviceConfig["brightnessFactor"].toDouble(1.0);
	brightnessMin = deviceConfig["brightnessMin"].toDouble(0.0);
	brightnessMax = deviceConfig["brightnessMax"].toDouble(1.0);
	groupId = deviceConfig["groupId"].toInt(0);
	logCommands = deviceConfig["logCommands"].toBool(false);

	_deviceConfig = deviceConfig;

	LedDevice::init(deviceConfig);

	return true;
}

LedDevicePhilipsHueEntertainment::~LedDevicePhilipsHueEntertainment()
{
	switchOff();
	//_bridge->deleteLater();
	Debug(_log, "Philips Hue Entertainment deconstructed");
}

void LedDevicePhilipsHueEntertainment::initBridge()
{
	Debug(_log, "Init and connect Philips Hue Bridge");
	PhilipsHueBridge::start();
	/*
	if(_bridge == nullptr)
	{
		_bridge = new PhilipsHueBridge(_log, _devConfig);

		connect(_bridge, &PhilipsHueBridge::newGroups, this, &LedDevicePhilipsHueEntertainment::newGroups);
		connect(_bridge, &PhilipsHueBridge::newLights, this, &LedDevicePhilipsHueEntertainment::newLights);
		connect(_bridge, &PhilipsHueBridge::updateLights, this, &LedDevicePhilipsHueEntertainment::updateLights);
		connect(_bridge, &PhilipsHueBridge::restoreOriginalState, this, &LedDevicePhilipsHueEntertainment::restoreOriginalState);
	}
	if(_bridge != nullptr && (lights.empty() || lightIds.empty()))
	{
		_bridge->bridgeConnect();
	}
	*/
	bridgeConnect();
}

void LedDevicePhilipsHueEntertainment::cleanupStreamer()
{
	if(_streamer != nullptr)
	{
		Debug(_log, "cleanup Philips Hue Entertainment Streamer");

		QThread* oldThread = _streamer->thread();
		disconnect(oldThread, 0, 0, 0);
		oldThread->quit();
		oldThread->wait();
		oldThread->deleteLater();

		disconnect(_streamer, 0, 0, 0);
		delete _streamer;
		_streamer = nullptr;
	}
}

void LedDevicePhilipsHueEntertainment::startStreaming()
{
	if(_prepareSwitchOff) return;

	if (!_isStreaming)
	{   
		if(!_prepareStreaming) 
		{
			Debug(_log, "Prepare Streaming");
			switchOn();
		}
		return;
	}

	if (_streamer == nullptr || !_streamer->_isRunning)
	{
		cleanupStreamer();
		Info(_log, "Connecting Philips Hue Entertainment Streamer");

		QThread* thread = new QThread(this);
		_streamer = new HueEntertainmentStreamer(_log, &lights, _deviceConfig);
		_streamer->moveToThread(thread);

		// setup thread management
		connect(thread, &QThread::started, _streamer, &HueEntertainmentStreamer::checkStreamGroupState);
		connect(thread, &QThread::finished, thread, &QThread::deleteLater);
		connect(thread, &QThread::finished, _streamer, &HueEntertainmentStreamer::deleteLater);

		connect(_streamer, &HueEntertainmentStreamer::getStreamGroupState, this, &PhilipsHueBridge::getStreamGroupState);
		connect(_streamer, &HueEntertainmentStreamer::setStreamGroupState, this, &PhilipsHueBridge::setStreamGroupState);
		connect(this, &PhilipsHueBridge::buildStreamer, _streamer, &HueEntertainmentStreamer::buildStreamer);
		//connect(_bridge, &PhilipsHueBridge::buildStreamer, _streamer, &HueEntertainmentStreamer::buildStreamer);
		//connect(_bridge, &PhilipsHueBridge::prepareStreamGroup, _streamer, &HueEntertainmentStreamer::prepareStreamGroup);
		
		thread->start();
	}

	if(_isStreaming && _streamer->_isRunning && _streamer->_hasBridgeConnection)
	{
		if(!_streamer->_isStreaming)
		{
			Info(_log, "Start Philips Hue Entertainment Streaming");
			_streamer->startStreamer();
		}
		_streamer->streamToBridge();
	}
}

int LedDevicePhilipsHueEntertainment::switchOn()
{
	_prepareStreaming = true;

	if(!_firstSwitchOn)
	{
		Debug(_log, "Update saved Light states");
		onlyUpdates = true;
		sendRequest("GET", "lights");
	}
	else
	{
		_firstSwitchOn = false;
	}

	_isStreaming = true;
	_prepareSwitchOff = false;

	return 0;
}

void LedDevicePhilipsHueEntertainment::exitRoutine()
{
	QString route = QString("groups/%1").arg(groupId);
	QString content = QString("{\"stream\":{\"active\":false}}");

	QString url = QString("http://%1/api/%2/%3").arg(address).arg(username).arg(route);

	QString debugMsg = (route!="") ? QString("/%1").arg(route) : url;
	Debug(_log, "PUT %s %s", QSTRING_CSTR(debugMsg), QSTRING_CSTR(content));

	QNetworkAccessManager* _networkmanager = new QNetworkAccessManager();

	QNetworkRequest request(url);
	QNetworkReply* reply = _networkmanager->get(request);
	//QNetworkReply* reply = _networkmanager->put(request, content.toLatin1());
	// Connect requestFinished signal to quit slot of the loop.
	QEventLoop loop;
	loop.connect(reply, SIGNAL(finished()), SLOT(quit()));
	// Go into the loop until the request is finished.
	loop.exec();

	qDebug() << "operation: " << reply->operation() << ", error: " << reply->error() << ", isRunning: " << reply->isRunning() << ", isFinished: " << reply->isFinished();
	qDebug() << "networkAccessible: " << reply->manager()->networkAccessible();

	if(reply->error() == QNetworkReply::NoError)
	{
		QByteArray response = reply->readAll();

		qDebug() << "request url: " << reply->url().toString();
		int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
		qDebug() << "statusCode: " << statusCode << ", reply:" << qPrintable(response) << ", reason: " << reason;

		QJsonParseError error;
		QJsonDocument doc = QJsonDocument::fromJson(response, &error);
		if (error.error != QJsonParseError::NoError)
		{
			Error(_log, "Got invalid response from bridge");
			qDebug() << "error: " << error.error << ", " << error.errorString() << ", " << error.offset;
			reply->deleteLater();
			//exitRoutine();
			return;
		}
	}
	// Free space.
	reply->deleteLater();
}

int LedDevicePhilipsHueEntertainment::switchOff()
{
	if(!_prepareSwitchOff)
	{
		_prepareSwitchOff = true;
		if (_isStreaming)
		{
			Debug(_log, "Switch Off Stream - Streaming active");
			_isStreaming = false;
			if(_streamer != nullptr)
			{
				_streamer->stopStreamer();
			}
			//exitRoutine();
			setStreamGroupState(false, false);
			cleanupStreamer();
		}
		else
		{
			Debug(_log, "Switch Off - no active Stream");
			setStreamGroupState(false, false);
		}
		_prepareStreaming = false;
	}
	return 0;
}

void LedDevicePhilipsHueEntertainment::newGroups(QMap<quint16, QJsonObject> map)
{
        // search user groupid inside map and create light if found
        if(map.contains(groupId))
        {
		QJsonObject group = map.value(groupId);

		if(group.value("type") == "Entertainment")
		{
			lightIds.clear();
			groupname = group.value("name").toString().trimmed().replace("\"", "");
			_deviceConfig["groupname"] = groupname;
			Info(_log,"Entertainment Group \"%s\" (ID %d) found", QSTRING_CSTR(groupname), groupId);
			QJsonArray jsonLights = group.value("lights").toArray();
			for(const auto id: jsonLights)
			{
				unsigned int lID = id.toString().toInt();
				if(std::find(lightIds.begin(), lightIds.end(), lID) == lightIds.end())
				{
					lightIds.emplace_back(lID);
				}
			}
			std::sort(lightIds.begin(),lightIds.end());
			Info(_log,"%d Lights in \"%s\" Group (ID %d) found", lightIds.size(), QSTRING_CSTR(groupname), groupId);
		}
		else
		{
			Error(_log,"Group ID %d is not an entertainment group", groupId);
		}
        }
        else
        {
		Error(_log,"Group ID %d isn't used on this bridge", groupId);
        }
}

void LedDevicePhilipsHueEntertainment::newLights(QMap<quint16, QJsonObject> map)
{
	if(!lightIds.empty())
	{
		// search user lightid inside map and create light if found
		lights.clear();
		unsigned int ledidx = 0;
		lights.reserve(lightIds.size());
		for(const auto id : lightIds)
		{
			if (map.contains(id))
			{
				lights.emplace_back(_log, id, map.value(id), ledidx);
			}
			else
			{
				Error(_log,"Light id %d isn't used on this bridge", id);
			}
			ledidx++;
		}
	}
	Info(_log,"%d Lights, %d IDs in \"%s\" Group (ID %d) created", lights.size(), lightIds.size(), QSTRING_CSTR(groupname), groupId);
}

void LedDevicePhilipsHueEntertainment::updateLights(QMap<quint16, QJsonObject> map)
{
	if(!lightIds.empty() && !lights.empty())
	{
		// search user lightid inside map and update lights
		unsigned int ledidx = 0;
		for(const auto id : lightIds)
		{
			if (map.contains(id))
			{
				PhilipsHueLight& lamp = lights.at(ledidx);
				lamp.saveOriginalState(map.value(id));
				ledidx++;
			}
		}
	}
}

int LedDevicePhilipsHueEntertainment::write(const std::vector<ColorRgb> & ledValues)
{
	// lights will be empty sometimes
	if(lights.empty()) return -1;

	if(_isActive)
	{
		if(_isStreaming)
		{
			unsigned int idx = 0;
			for (PhilipsHueLight& light : lights)
			{
				// Get color.
				ColorRgb color = ledValues.at(idx);
				// Scale colors from [0, 255] to [0, 1] and convert to xy space.
				CiColor xy = CiColor::rgbToCiColor(color.red / 255.0, color.green / 255.0, color.blue / 255.0, light.getColorSpace());
				if(xy != light.getColor())
				{
					light.setColor(xy, true, brightnessFactor, brightnessMin, brightnessMax);
				}
				idx++;
			}
		}
		startStreaming();
	}

	return 0;
}

void LedDevicePhilipsHueEntertainment::restoreOriginalState() 
{
	if(!lightIds.empty())
	{
		for (PhilipsHueLight& light : lights)
		{
			setLightState(light.getId(),light.getOriginalState());
		}
	}
}

void LedDevicePhilipsHueEntertainment::stateChanged(bool newState)
{
	if(_isActive != newState)
	{
		if(newState)
		{
			if(lights.empty() || lightIds.empty()) initBridge();
			_prepareSwitchOff = false;
		} 
		else
		{
			switchOff();
		}
	}
	_isActive = newState;
}

HueEntertainmentStreamer::HueEntertainmentStreamer(Logger* log, std::vector<PhilipsHueLight>* lights, const QJsonObject &deviceConfig)
	: _isRunning(true)
	, _hasBridgeConnection(false)
	, _isStreaming(false)
	, _log(log)
	, lights(lights)
	, _deviceConfig(deviceConfig)
	, client_fd()
	, entropy()
	, ssl()
	, conf()
	, ctr_drbg()
	, timer()
	, retry_left(MAX_RETRY)
{
	_streamFrequency = _deviceConfig["streamFrequency"].toInt(50);
	_debugStreamer = _deviceConfig["debugStreamer"].toBool(false);
	_debugLevel = _deviceConfig["debugLevel"].toInt(0);
	logCommands = _deviceConfig["logCommands"].toBool(false);
	address = _deviceConfig["output"].toString("");
	username = _deviceConfig["username"].toString("");
	clientkey = _deviceConfig["clientkey"].toString("");
	groupId = _deviceConfig["groupId"].toInt(0);
	groupname = _deviceConfig["groupname"].toString();

	#define DEBUG_LEVEL _debugLevel
}
/*
void HueEntertainmentStreamer::prepareStreamGroup()
{
	Info(_log, "Switch On Streaming Group: \"%s\" (ID %d)", QSTRING_CSTR(groupname), groupId);
	emit setStreamGroupState(true);
}
*/
void HueEntertainmentStreamer::checkStreamGroupState()
{
	Info(_log, "check Streaming Group: \"%s\" (ID %d) state", QSTRING_CSTR(groupname), groupId);
	emit getStreamGroupState(true);
}

void HueEntertainmentStreamer::buildStreamer()
{
	Debug(_log, "start Build Philips Hue Entertainment Streamer");

	if(!_isRunning) return;

	QMutexLocker lock(&_hueMutex);

	if(!initStreamer()) goto exit;

	if(!_isRunning) return;

	if(!startUPDConnection()) goto exit;

	if(!_isRunning) return;

	if(!startSSLHandshake()) goto exit;

	return;

	exit:
		_isRunning = false;
}

bool HueEntertainmentStreamer::initStreamer()
{
	mbedtls_net_init(&client_fd);
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_x509_crt_init(&cacert);
	mbedtls_ctr_drbg_init(&ctr_drbg);
	if(!seedingRNG()) return false;
	return setupStructure();
}

bool HueEntertainmentStreamer::startUPDConnection()
{
	int ret;

	mbedtls_ssl_session_reset(&ssl);

	if(!setupPSK()) return false;

	if(_debugStreamer) qDebug() << "Connecting to udp" << address << SERVER_PORT;

	if ((ret = mbedtls_net_connect(&client_fd, address.toUtf8(), SERVER_PORT, MBEDTLS_NET_PROTO_UDP)) != 0)
	{
		if(_debugStreamer) qCritical() << "mbedtls_net_connect FAILED" << ret;
		return false;
	}

	mbedtls_ssl_set_bio(&ssl, &client_fd, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
	mbedtls_ssl_set_timer_cb(&ssl, &timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);

	if(_debugStreamer) qDebug() << "Connecting...ok";

	return true;
}

bool HueEntertainmentStreamer::startSSLHandshake()
{
	int ret;

	if(_debugStreamer) qDebug() << "Performing the SSL/TLS handshake...";

	for (int attempt = 1; attempt < 5; ++attempt)
	{

		if(_debugStreamer) qDebug() << "handshake attempt" << attempt;

		do
		{
			ret = mbedtls_ssl_handshake(&ssl);
			if(!_isRunning) break;
		} 
		while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

		if (ret == 0) break;

		QThread::msleep(200);
	}

	if (ret != 0)
	{
		if(_debugStreamer) qCritical() << "mbedtls_ssl_handshake FAILED";

		Error(_log, "Philips Hue Entertaiment Streamer Connection failed!");

#if DEBUG_LEVEL > 0
#ifdef MBEDTLS_ERROR_C
		char error_buf[100];
		mbedtls_strerror(ret, error_buf, 100);
		mbedtls_printf("Last error was: %d - %s\n\n", ret, error_buf);
#endif
#endif
		return false;
	}

	if(_debugStreamer) qDebug() << "Performing the SSL/TLS handshake...ok";

	Info(_log, "Philips Hue Entertaiment Streamer successful connected! Ready for Streaming.");

	_hasBridgeConnection = true;

	return true;
}

bool HueEntertainmentStreamer::seedingRNG()
{
	int ret;

	if(_debugStreamer) qDebug() << "Seeding the random number generator...";

	mbedtls_entropy_init(&entropy);

	if(_debugStreamer) qDebug() << "Set mbedtls_ctr_drbg_seed...";

	if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const unsigned char *>(pers), strlen(pers))) != 0)
	{
		if(_debugStreamer) mbedtls_printf(" failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret);
		return false;
	}

	if(_debugStreamer) qDebug() << "Seeding the random number generator...ok";

	return true;
}

bool HueEntertainmentStreamer::setupPSK()
{
	int ret;

	QByteArray pskArray = clientkey.toUtf8();
	QByteArray pskRawArray = QByteArray::fromHex(pskArray);

	QByteArray pskIdArray = username.toUtf8();
	QByteArray pskIdRawArray = pskIdArray;

	if (0 != (ret = mbedtls_ssl_conf_psk(&conf, (const unsigned char*)pskRawArray.data(), pskRawArray.length() * sizeof(char), reinterpret_cast<const unsigned char *>(pskIdRawArray.data()), pskIdRawArray.length() * sizeof(char))))
	{
		if(_debugStreamer) qCritical() << "mbedtls_ssl_conf_psk FAILED" << ret;
		return false;
	}

	return true;
}

bool HueEntertainmentStreamer::setupStructure()
{
	int ret;

	if(_debugStreamer) qDebug() << "Setting up the structure...";

	if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
	{
		if(_debugStreamer) qCritical() << "mbedtls_ssl_config_defaults FAILED" << ret;
		return false;
	}

	mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
	//mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
	//mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
	mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
	mbedtls_ssl_conf_ciphersuites(&conf, ciphers);
	mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

#if DEBUG_LEVEL > 0
	mbedtls_ssl_conf_dbg(&conf, HueEntertainmentStreamerDebug, NULL);
	mbedtls_debug_set_threshold(DEBUG_LEVEL);
#endif
	
	mbedtls_ssl_conf_handshake_timeout(&conf, 400, 1000);

	if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0)
	{
		if(_debugStreamer) qCritical() << "mbedtls_ssl_setup FAILED" << ret;
		return false;
	}

	if ((ret = mbedtls_ssl_set_hostname(&ssl, SERVER_NAME)) != 0)
	{
		if(_debugStreamer) qCritical() << "mbedtls_ssl_set_hostname FAILED" << ret;
		return false;
	}

	if(_debugStreamer) qDebug() << "Setting up the structure...ok";

	return true;
}

HueEntertainmentStreamer::~HueEntertainmentStreamer()
{
	if(_isStreaming || _isRunning || _hasBridgeConnection) stopStreamer();
	if(_debugStreamer) qDebug() << "Philips Hue Entertainment Streamer deconstructed";
}

void HueEntertainmentStreamer::startStreamer()
{
	_isStreaming = true;
}

void HueEntertainmentStreamer::stopStreamer()
{
	_isStreaming = false;
	_isRunning = false;

	Info(_log, "Switch Off Streaming Group: \"%s\" (ID %d)", QSTRING_CSTR(groupname), groupId);
	//emit getStreamGroupState(false);

	if(_debugStreamer) qDebug() << "Stop Philips Hue Entertainment Streamer";

	_hasBridgeConnection = false;

	try
	{
		mbedtls_net_free(&client_fd);
		mbedtls_ssl_free(&ssl);
		mbedtls_ssl_config_free(&conf);
		mbedtls_x509_crt_free(&cacert);
		mbedtls_ctr_drbg_free(&ctr_drbg);
		mbedtls_entropy_free(&entropy);
		if(_debugStreamer) qDebug() << "Philips Hue Entertainment Streamer successful stopped!";
	}
	catch (std::exception &e)
	{
		qDebug() << "stop Streamer Error: " << e.what();
	}
	catch (...)
	{
		qDebug() << "stop Streamer Error: <unknown>";
	}     	
}

QByteArray HueEntertainmentStreamer::prepareMsgData()
{
	static const uint8_t HEADER[] = 
	{
		'H', 'u', 'e', 'S', 't', 'r', 'e', 'a', 'm', //protocol
		0x01, 0x00, //version 1.0
		0x01, //sequence number 1
		0x00, 0x00, //Reserved write 0’s
		0x01, //xy Brightness
		0x00, // Reserved, write 0’s
	};

	static const uint8_t PAYLOAD_PER_LIGHT[] =
	{
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
	Msg.reserve(static_cast<int>(sizeof(HEADER) + sizeof(PAYLOAD_PER_LIGHT) * lights->size()));
	Msg.append((char*)HEADER, sizeof(HEADER));

	for (const PhilipsHueLight& lamp : *lights)
	{
		CiColor lampC = lamp.getColor();
		quint64 R = lampC.x * 0xffff;
		quint64 G = lampC.y * 0xffff;
		quint64 B = lampC.bri * 0xffff;
		unsigned int id = lamp.getId();
		const uint8_t payload[] = {
			0x00, 0x00, static_cast<uint8_t>(id),
			static_cast<uint8_t>((R >> 8) & 0xff), static_cast<uint8_t>(R & 0xff),
			static_cast<uint8_t>((G >> 8) & 0xff), static_cast<uint8_t>(G & 0xff),
			static_cast<uint8_t>((B >> 8) & 0xff), static_cast<uint8_t>(B & 0xff)
		};
		Msg.append((char*)payload, sizeof(payload));
	}

	return Msg;
}

void HueEntertainmentStreamer::streamToBridge()
{
	if (!_isRunning || !_isStreaming) return;

	QMutexLocker lock(&_hueMutex);

	int ret;

	auto frequency((int64_t) ((1.0 / _streamFrequency) * 1000));

	using Time = std::chrono::steady_clock;

	auto timeStart = Time::now();

	QByteArray Msg = prepareMsgData();

	int len = Msg.size();

	do
	{
		ret = mbedtls_ssl_write(&ssl, reinterpret_cast<unsigned char *>(Msg.data()), len);
	}
	while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

	if (ret <= 0)
	{
		handleReturn(ret);
	}

	auto timeEnd = std::chrono::duration_cast<std::chrono::milliseconds>(Time::now() - timeStart).count();

	auto msleep = frequency - timeEnd;

	if (msleep > 0)
	{
		QThread::msleep(msleep);
	}
}

void HueEntertainmentStreamer::handleReturn(int ret)
{
	bool closeNotify = false;
	bool gotoExit = false;

	switch (ret)
	{
		case MBEDTLS_ERR_SSL_TIMEOUT:
			if(_debugStreamer) qWarning() << "timeout";
			if (retry_left-- > 0) return;
			gotoExit = true;
			break;

		case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
			if(_debugStreamer) qWarning() << "Connection was closed gracefully";
			ret = 0;
			closeNotify = true;
			break;

		default:
			if(_debugStreamer) qWarning() << "mbedtls_ssl_read returned" << ret;
			gotoExit = true;
	}

	if (closeNotify)
	{
		if(_debugStreamer) qDebug() << "Closing the connection...";
		/* No error checking, the connection might be closed already */
		do
		{
			ret = mbedtls_ssl_close_notify(&ssl);
		}
		while (ret == MBEDTLS_ERR_SSL_WANT_WRITE);
		ret = 0;
		if(_debugStreamer) qDebug() << "Connection successful closed";
		gotoExit = true;
	}

	if (gotoExit)
	{
		if(_debugStreamer) qDebug() << "Exit Philips Hue Entertainment Streamer.";
		_isRunning = false;
	}
}
