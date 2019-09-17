// Local-Hyperion includes
#include "LedDevicePhilipsHue.h"

// qt includes
#include <QtCore/qmath.h>
#include <QNetworkReply>
#include <QDebug>

#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonDocument>

bool operator ==(CiColor p1, CiColor p2)
{
	return ((p1.x == p2.x) && (p1.y == p2.y) && (p1.bri == p2.bri));
}

bool operator !=(CiColor p1, CiColor p2)
{
	return !(p1 == p2);
}

CiColor CiColor::rgbToCiColor(double red, double green, double blue, CiColorTriangle colorSpace)
{
	double cx;
	double cy;
	double bri;

	if(red + green + blue > 0)
	{

		// Apply gamma correction.
		double r = (red > 0.04045) ? pow((red + 0.055) / (1.0 + 0.055), 2.4) : (red / 12.92);
		double g = (green > 0.04045) ? pow((green + 0.055) / (1.0 + 0.055), 2.4) : (green / 12.92);
		double b = (blue > 0.04045) ? pow((blue + 0.055) / (1.0 + 0.055), 2.4) : (blue / 12.92);
		
		// Convert to XYZ space.
		double X = r * 0.664511 + g * 0.154324 + b * 0.162028;
		double Y = r * 0.283881 + g * 0.668433 + b * 0.047685;
		double Z = r * 0.000088 + g * 0.072310 + b * 0.986039;

		cx = X / (X + Y + Z);
		cy = Y / (X + Y + Z);

		// RGB to HSV/B Conversion before gamma correction V/B for brightness, not Y from XYZ Space.
		// bri = std::max(std::max(red, green), blue);
		// RGB to HSV/B Conversion after gamma correction V/B for brightness, not Y from XYZ Space.
		bri = std::max(r, std::max(g, b));
	}
	else
	{
		cx = 0.0;
		cy = 0.0;
		bri = 0.0;
	}

	if (std::isnan(cx))
	{
		cx = 0.0;
	}
	if (std::isnan(cy))
	{
		cy = 0.0;
	}
	if (std::isnan(bri))
	{
		bri = 0.0;
	}

	CiColor xy = { cx, cy, bri };

	if(red + green + blue > 0)
	{
		// Check if the given XY value is within the color reach of our lamps.
		if (!isPointInLampsReach(xy, colorSpace))
		{
			// It seems the color is out of reach let's find the closes color we can produce with our lamp and send this XY value out.
			CiColor pAB = getClosestPointToPoint(colorSpace.red, colorSpace.green, xy);
			CiColor pAC = getClosestPointToPoint(colorSpace.blue, colorSpace.red, xy);
			CiColor pBC = getClosestPointToPoint(colorSpace.green, colorSpace.blue, xy);
			// Get the distances per point and see which point is closer to our Point.
			double dAB = getDistanceBetweenTwoPoints(xy, pAB);
			double dAC = getDistanceBetweenTwoPoints(xy, pAC);
			double dBC = getDistanceBetweenTwoPoints(xy, pBC);
			double lowest = dAB;
			CiColor closestPoint = pAB;
			if (dAC < lowest)
			{
				lowest = dAC;
				closestPoint = pAC;
			}
			if (dBC < lowest)
			{
				lowest = dBC;
				closestPoint = pBC;
			}
			// Change the xy value to a value which is within the reach of the lamp.
			xy.x = closestPoint.x;
			xy.y = closestPoint.y;
		}
	}
	return xy;
}

double CiColor::crossProduct(CiColor p1, CiColor p2)
{
	return p1.x * p2.y - p1.y * p2.x;
}

bool CiColor::isPointInLampsReach(CiColor p, CiColorTriangle colorSpace)
{
	CiColor v1 = { colorSpace.green.x - colorSpace.red.x, colorSpace.green.y - colorSpace.red.y };
	CiColor v2 = { colorSpace.blue.x - colorSpace.red.x, colorSpace.blue.y - colorSpace.red.y };
	CiColor  q = { p.x - colorSpace.red.x, p.y - colorSpace.red.y };
	double s = crossProduct(q, v2) / crossProduct(v1, v2);
	double t = crossProduct(v1, q) / crossProduct(v1, v2);
	if ((s >= 0.0) && (t >= 0.0) && (s + t <= 1.0))
	{
		return true;
	}
	return false;
}

CiColor CiColor::getClosestPointToPoint(CiColor a, CiColor b, CiColor p)
{
	CiColor AP = { p.x - a.x, p.y - a.y };
	CiColor AB = { b.x - a.x, b.y - a.y };
	double ab2 = AB.x * AB.x + AB.y * AB.y;
	double ap_ab = AP.x * AB.x + AP.y * AB.y;
	double t = ap_ab / ab2;
	if (t < 0.0)
	{
		t = 0.0;
	}
	else if (t > 1.0)
	{
		t = 1.0;
	}
	return { a.x + AB.x * t, a.y + AB.y * t };
}

double CiColor::getDistanceBetweenTwoPoints(CiColor p1, CiColor p2)
{
	// Horizontal difference.
	double dx = p1.x - p2.x;
	// Vertical difference.
	double dy = p1.y - p2.y;
	// Absolute value.
	return sqrt(dx * dx + dy * dy);
}

const std::set<QString> PhilipsHueLight::GAMUT_A_MODEL_IDS = { "LLC001", "LLC005", "LLC006", "LLC007", "LLC010", "LLC011", "LLC012", "LLC013", "LLC014", "LST001" };
const std::set<QString> PhilipsHueLight::GAMUT_B_MODEL_IDS = { "LCT001", "LCT002", "LCT003", "LCT007", "LLM001" };
const std::set<QString> PhilipsHueLight::GAMUT_C_MODEL_IDS = { "LLC020", "LST002", "LCT011", "LCT012", "LCT010", "LCT014", "LCT015", "LCT016", "LCT024" };

PhilipsHueLight::PhilipsHueLight(Logger* log, unsigned int id, QJsonObject values, unsigned int ledidx)
	: _log(log)
	, id(id)
	, ledidx(ledidx)
	, on(false)
	, transitionTime(0)
	, logCommands(false)
	, colorBlack({0.0, 0.0, 0.0})
{
	modelId = values["modelid"].toString().trimmed().replace("\"", "");
	// Find id in the sets and set the appropriate color space.
	if (GAMUT_A_MODEL_IDS.find(modelId) != GAMUT_A_MODEL_IDS.end())
	{
		Debug(_log, "Recognized model id %s of light ID %d as gamut A", QSTRING_CSTR(modelId), id);
		colorSpace.red		= {0.704, 0.296};
		colorSpace.green	= {0.2151, 0.7106};
		colorSpace.blue		= {0.138, 0.08};
		colorBlack 		= {0.138, 0.08, 0.0};
	}
	else if (GAMUT_B_MODEL_IDS.find(modelId) != GAMUT_B_MODEL_IDS.end())
	{
		Debug(_log, "Recognized model id %s of light ID %d as gamut B", QSTRING_CSTR(modelId), id);
		colorSpace.red 		= {0.675, 0.322};
		colorSpace.green	= {0.409, 0.518};
		colorSpace.blue 	= {0.167, 0.04};
		colorBlack 		= {0.167, 0.04, 0.0};
	}
	else if (GAMUT_C_MODEL_IDS.find(modelId) != GAMUT_C_MODEL_IDS.end())
	{
		Debug(_log, "Recognized model id %s of light ID %d as gamut C", QSTRING_CSTR(modelId), id);
		colorSpace.red		= {0.6915, 0.3083};
		colorSpace.green	= {0.17, 0.7};
		colorSpace.blue 	= {0.1532, 0.0475};
		colorBlack 		= {0.1532, 0.0475, 0.0};
	}
	else
	{
		Warning(_log, "Did not recognize model id %s of light ID %d", QSTRING_CSTR(modelId), id);
		colorSpace.red 		= {1.0, 0.0};
		colorSpace.green 	= {0.0, 1.0};
		colorSpace.blue 	= {0.0, 0.0};
		colorBlack 		= {0.0, 0.0, 0.0};
	}

	//logCommands = _bridge->logCommands;
	logCommands = true;
	saveOriginalState(values);
	//setTransitionTime(_bridge->transitionTime);
	// Determine the model id.
	lightname = values["name"].toString().trimmed().replace("\"", "");
	Info(_log,"Light ID %d (\"%s\", LED index \"%d\") created", id, QSTRING_CSTR(lightname), ledidx);
}

PhilipsHueLight::~PhilipsHueLight()
{
	//_bridge->setLightState(id, originalState);
}

unsigned int PhilipsHueLight::getId() const
{
	return id;
}

QString PhilipsHueLight::getOriginalState()
{
	return originalState;
}

void PhilipsHueLight::set(QString state)
{
	//_bridge->sendRequest("PUT", QString("lights/%1/state").arg(id), state);
}

void PhilipsHueLight::saveOriginalState(QJsonObject values)
{
	// Get state object values which are subject to change.
	if (!values["state"].toObject().contains("on"))
	{
		Error(_log, "Got invalid state object from light ID %d", id);
	}
	QJsonObject lState = values["state"].toObject();
	originalStateJSON = lState;
	QJsonObject state;
	state["on"] = lState["on"];
	originalColor = colorBlack;
	QString c;
	if (state["on"].toBool())
	{
		state["xy"] = lState["xy"];
		state["bri"] = lState["bri"];
		on = true;
		color = {
				state["xy"].toArray()[0].toDouble(),
				state["xy"].toArray()[1].toDouble(),
				state["bri"].toDouble() / 254.0
			};
		originalColor = color;
		if(logCommands)
		{
			c = QString("{ \"xy\": [%1, %2], \"bri\": %3 }").arg(originalColor.x, 0, 'd', 4).arg(originalColor.y, 0, 'd', 4).arg((originalColor.bri * 254.0), 0, 'd', 4);
			Debug(_log, "originalColor state on: %s", QSTRING_CSTR(c));
		}
		transitionTime = values["state"].toObject()["transitiontime"].toInt();
	}
	// Determine the original state.
	originalState = QJsonDocument(state).toJson(QJsonDocument::JsonFormat::Compact).trimmed();
}

void PhilipsHueLight::setOn(bool on)
{
	if (this->on != on)
	{
		QString arg = on ? "true" : "false";
		set(QString("{ \"on\": %1 }").arg(arg));
	}
	this->on = on;
}

void PhilipsHueLight::setTransitionTime(unsigned int transitionTime)
{
	if (this->transitionTime != transitionTime)
	{
		set(QString("{ \"transitiontime\": %1 }").arg(transitionTime));
	}
	this->transitionTime = transitionTime;
}

void PhilipsHueLight::setColor(CiColor color, bool isStream, double brightnessFactor, double brightnessMin, double brightnessMax)
{
	const int bri = qRound(qMin(254.0, brightnessFactor * qMax(1.0, color.bri * 254.0)));
	if (this->color != color)
	{
		if(!isStream)
		{
			QString c = QString("{ \"xy\": [%1, %2], \"bri\": %3 }").arg(color.x, 0, 'd', 4).arg(color.y, 0, 'd', 4).arg(bri);
			set(c);
		}
		else
		{
			if(brightnessMin < 0.0) brightnessMin = 0.0;
			if(brightnessMax > 1.0) brightnessMax = 1.0;
			color.bri = (qMin(brightnessMax, brightnessFactor * qMax(brightnessMin, color.bri)));
			//if(color.x == 0.0 && color.y == 0.0) color = colorBlack;
		}
		this->color = color;
	}
}

CiColor PhilipsHueLight::getColor() const
{
	return color;
}

CiColorTriangle PhilipsHueLight::getColorSpace() const
{
	return colorSpace;
}

LedDevice* LedDevicePhilipsHue::construct(const QJsonObject &deviceConfig)
{
	return new LedDevicePhilipsHue(deviceConfig);
}

PhilipsHueBridge::PhilipsHueBridge(const QJsonObject &_devConfig)
	: LedDevice(_devConfig)
	, onlyUpdates(false)
	, checkGroupStreamState(false)
	, startStreaming(true)
{
}

void PhilipsHueBridge::start()
{
	qDebug() << "Philips Hue Bridge Thread start";

	address = _devConfig["output"].toString("");
	username = _devConfig["username"].toString("");
	clientkey = _devConfig["clientkey"].toString("");
	groupId = _devConfig["groupId"].toInt(0);
	logCommands = _devConfig["logCommands"].toBool(false);
	transitionTime = _devConfig["transitiontime"].toInt(0);

	// setup reconnection timer
	bTimer.setInterval(5000);
	bTimer.setSingleShot(true);
	
	_manager = new QNetworkAccessManager(this);

	connect(&bTimer, &QTimer::timeout, this, &PhilipsHueBridge::bridgeConnect);
	connect(_manager, &QNetworkAccessManager::finished, this, &PhilipsHueBridge::resolveReply);
}

PhilipsHueBridge::~PhilipsHueBridge()
{
	if(logCommands) Debug(_log, "Philips Hue Bridge deconstruced");
	delete _manager;
}

void PhilipsHueBridge::getStreamGroupState(bool startStreaming)
{
	checkGroupStreamState = true;
	this->startStreaming = startStreaming;
	QString startStreamingActive = startStreaming ? "true" : "false";
	if(logCommands) Debug(_log, "get group ID %d stream state, startStreaming: %s", groupId, QSTRING_CSTR(startStreamingActive));
	sendRequest("GET", QString("groups/%1").arg(groupId));
}

void PhilipsHueBridge::setStreamGroupState(bool state, bool startStreaming)
{
	checkGroupStreamState = true;
	this->startStreaming = startStreaming;
	QString startStreamingActive = startStreaming ? "true" : "false";
	QString active = state ? "true" : "false";
	if(logCommands) Debug(_log, "set group ID %d stream state \"active\": %s, startStreaming: %s", groupId, QSTRING_CSTR(active), QSTRING_CSTR(startStreamingActive));
	sendRequest("PUT", QString("groups/%1").arg(groupId), QString("{\"stream\":{\"active\":%1}}").arg(active));
}

void PhilipsHueBridge::setLightState(const unsigned int lightId, QString state)
{
	if(logCommands) Debug(_log, "setLightState: %s", QSTRING_CSTR(state));
	sendRequest("PUT", QString("lights/%1/state").arg(lightId), QString(state));
}

void PhilipsHueBridge::bridgeConnect(void)
{
	if(username.isEmpty() || address.isEmpty())
	{
		Error(_log,"Username or IP Address is empty!");
	}
	else
	{
		Debug(_log, "Connect to bridge");
		sendRequest("GET");
	}
}

void PhilipsHueBridge::sendRequest(QString method, QString route, QString content)
{
	if(method == "GET" || method == "PUT")
	{
		QString url = QString("http://%1/api/%2/%3").arg(address).arg(username).arg(route);
		if(logCommands || route=="")
		{
			QString debugMsg = (route!="") ? QString("/%1").arg(route) : url;
			Debug(_log, "%s %s %s", QSTRING_CSTR(method), QSTRING_CSTR(debugMsg), QSTRING_CSTR(content));
		}

		QNetworkRequest request(url);

		if(method == "GET")
		{
			_manager->get(request);
		}
		else if(method == "PUT")
		{
			_manager->put(request, content.toLatin1());
		}
	}
}

void PhilipsHueBridge::resolveReply(QNetworkReply* const &reply)
{
	reply->deleteLater();
	//if(logCommands) 
	// TODO use put request also for network error checking with decent threshold

	if(reply->operation() == QNetworkAccessManager::GetOperation || reply->operation() == QNetworkAccessManager::PutOperation)
	{
		if(reply->error() == QNetworkReply::NoError)
		{
			QByteArray response = reply->readAll();
			QJsonParseError error;
			QJsonDocument doc = QJsonDocument::fromJson(response, &error);
			if (error.error != QJsonParseError::NoError)
			{
				Error(_log, "Got invalid response from bridge");
				qDebug() << "url: " << reply->url().toString();
				qDebug() << "error: " << error.error << ", " << error.errorString() << ", " << error.offset;
				int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				qDebug() << "statusCode: " << statusCode << ", reply:" << qPrintable(response);
				if(onlyUpdates) onlyUpdates = false;
				return;
			}
			if(reply->operation() == QNetworkAccessManager::PutOperation)
			{
				Debug(_log, "PutOperation");
				QVariant rsp = doc.toVariant();
				QVariantMap map = rsp.toList().first().toMap();
				if (map.contains("error"))
				{
					QString errorMessage = map.value("error").toMap().value("description").toString();
					qWarning() << errorMessage;
					return;
				}

				if (!map.contains("success"))
				{
					qWarning() << "Neither error nor success contained in Bridge response...";
					return;
				}

				if(checkGroupStreamState)
				{
					if(groupId > 0) 
					{
						QString valueName = QString("/groups/%1/stream/active").arg(groupId);
						if(!map.value("success").toMap().value(valueName).isValid())
						{
							return;
						}
						else
						{
							bool groupStreamState = map.value("success").toMap().value(valueName).toBool();
							if(logCommands) qDebug() << "success" << valueName << groupStreamState << ", startStreaming: " << startStreaming;
							if(groupStreamState == true)
							{
								if(startStreaming)
								{
									Debug(_log, "Stream Group is ready, build Hue Streamer");
									emit buildStreamer();
								}
								else
								{
									Debug(_log, "Stream Group in use, recheck State");
									QThread::msleep(500);
									getStreamGroupState(startStreaming);
									return;
								}
							}
							else
							{
								if(startStreaming)
								{
									Debug(_log, "Stream Group not ready, recheck State");
									getStreamGroupState(startStreaming);
									return;
								}
								else
								{
									Debug(_log, "Restore Original Light States");
									restoreOriginalState();
								}
							}
						}
					}
					checkGroupStreamState = false;
				}
			}
			else if(reply->operation() == QNetworkAccessManager::GetOperation)
			{
				Debug(_log, "GetOperation");
				// check for authorization
				if(doc.isArray())
				{
					Error(_log, "Authorization failed, username invalid");
					if(onlyUpdates) onlyUpdates = false;
					return;
				}

				if(checkGroupStreamState)
				{
					if(groupId > 0)
					{
						QJsonObject obj = doc.object()["stream"].toObject();

						if(obj.isEmpty())
						{
							Error(_log, "no Streaming Infos in Group found");
							checkGroupStreamState = false;
							return;
						}

						bool streamState = obj.value("active").toBool();
						QString streamOwner = obj.value("owner").toString();
						if(logCommands) qDebug() << "Group Streaming State: " << streamState << ", streamOwner: " << streamOwner << ", startStreaming: " << startStreaming;
						if(streamState && streamOwner != "" && streamOwner == username)
						{
							setStreamGroupState(false, startStreaming);
							return;
						}
						if(startStreaming)
						{
							//Info(_log, "Switch On Streaming Group: \"%s\" (ID %d)", QSTRING_CSTR(groupname), groupId);
							Info(_log, "Switch On Streaming Group ID %d", groupId);
							setStreamGroupState(true, startStreaming);
							return;
						}
						else
						{
							restoreOriginalState();
						}
					}
					checkGroupStreamState = false;
					return;
				}
				else
				{
					QJsonObject obj;
					QStringList keys;
					QMap<quint16,QJsonObject> map;

					if(!onlyUpdates)
					{
						if(groupId > 0) 
						{
							obj = doc.object()["groups"].toObject();

							if(obj.isEmpty())
							{
								Error(_log, "Bridge has no registered groups");
								return;
							}

							// get all available group ids and their values
							keys = obj.keys();
							for (int i = 0; i < keys.size(); ++i)
							{
								unsigned int gID = keys.at(i).toInt();
								if(groupId==gID)
								{
									map.insert(gID, obj.take(keys.at(i)).toObject());
								}
							}
							Debug(_log, "new Group ready");
							newGroups(map);
						}
					}

					obj = (!onlyUpdates) ? doc.object()["lights"].toObject() : doc.object();

					if(obj.isEmpty())
					{
						Error(_log, "Bridge has no registered bulbs/stripes");
						if(onlyUpdates) onlyUpdates = false;
						return;
					}

					// get all available light ids and their values
					keys = obj.keys();
					map.clear();
					for (int i = 0; i < keys.size(); ++i)
					{
						map.insert(keys.at(i).toInt(), obj.take(keys.at(i)).toObject());
					}

					if(!onlyUpdates)
					{
						Debug(_log, "new Lights ready");
						newLights(map);
					}
					else
					{
						onlyUpdates = false;
						Debug(_log, "Lights updates ready");
						updateLights(map);
					}
				}
			}
		}
		else
		{
			Error(_log,"Network Error: %s", QSTRING_CSTR(reply->errorString()));
			if(reply->operation() == QNetworkAccessManager::GetOperation) bTimer.start();
		}
	}
}

LedDevicePhilipsHue::LedDevicePhilipsHue(const QJsonObject &deviceConfig)
	: PhilipsHueBridge(deviceConfig)
	, _isActive(true)
{

}

void LedDevicePhilipsHue::start()
{
	qDebug() << "Philips Hue Thread start";
	_deviceReady = init(_devConfig);
	initBridge();
	connect(this, &LedDevice::enableStateChanged, this, &LedDevicePhilipsHue::stateChanged);
}

bool LedDevicePhilipsHue::init(const QJsonObject &deviceConfig)
{
	brightnessFactor = deviceConfig["brightnessFactor"].toDouble(1.0);
	brightnessMin = deviceConfig["brightnessMin"].toDouble(0.0);
	brightnessMax = deviceConfig["brightnessMax"].toDouble(1.0);
	transitionTime = deviceConfig["transitiontime"].toInt(0);
	switchOffOnBlack = deviceConfig["switchOffOnBlack"].toBool(true);
	QJsonArray lArray = deviceConfig["lightIds"].toArray();

	QJsonObject newDC = deviceConfig;
	if(!lArray.empty())
	{
		lightIds.clear();
		for(const auto i : lArray)
		{
			unsigned int lID = i.toInt();
			if(std::find(lightIds.begin(), lightIds.end(), lID) == lightIds.end())
			{
				lightIds.emplace_back(lID);
			}
		}

		// adapt latchTime to count of user lightIds (bridge 10Hz max overall)
		newDC.insert("latchTime",QJsonValue(100*(int)lightIds.size()));
	}
	else
	{
		Error(_log,"No light ID provided, abort");
	}

	LedDevice::init(newDC);

	return true;
}

LedDevicePhilipsHue::~LedDevicePhilipsHue()
{
	restoreOriginalState();
	Debug(_log, "Philips Hue deconstructed");
}

void LedDevicePhilipsHue::initBridge()
{
	Debug(_log, "Init and connect Philips Hue Bridge");
	PhilipsHueBridge::start();
	/*
	if(_bridge == nullptr)
	{
		_bridge = new PhilipsHueBridge(_log, _devConfig);
		connect(_bridge, &PhilipsHueBridge::newLights, this, &LedDevicePhilipsHue::newLights);
	}
	if(_bridge != nullptr && (lights.empty() || lightIds.empty()))
	{
		_bridge->bridgeConnect();
	}
	*/
	bridgeConnect();
}

void LedDevicePhilipsHue::newLights(QMap<quint16, QJsonObject> map)
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
}

int LedDevicePhilipsHue::write(const std::vector<ColorRgb> & ledValues)
{
	// lights will be empty sometimes
	if(lights.empty()) return -1;

	// more lights then leds, stop always
	if(ledValues.size() < lights.size())
	{
		Error(_log,"More LightIDs configured than leds, each LightID requires one led!");
		return -1;
	}

	// Iterate through lights and set colors.
	unsigned int idx = 0;
	for (PhilipsHueLight& light : lights)
	{
		// Get color.
		ColorRgb color = ledValues.at(idx);
		// Scale colors from [0, 255] to [0, 1] and convert to xy space.
		CiColor xy = CiColor::rgbToCiColor(color.red / 255.0, color.green / 255.0, color.blue / 255.0, light.getColorSpace());

		if (switchOffOnBlack && xy.bri == 0)
		{
			light.setOn(false);
		}
		else
		{
			light.setOn(true);
			// Write color if color has been changed.
			light.setTransitionTime(transitionTime);
			light.setColor(xy, false, brightnessFactor, brightnessMin, brightnessMax);
		}
		idx++;
	}

	return 0;
}

void LedDevicePhilipsHue::restoreOriginalState() 
{
	if(!lightIds.empty())
	{
		for (PhilipsHueLight& light : lights)
		{
			//_bridge->setLightState(light.getId(),light.getOriginalState());
			setLightState(light.getId(),light.getOriginalState());
		}
	}
}

void LedDevicePhilipsHue::stateChanged(bool newState)
{
	if(_isActive != newState)
	{
		if(newState)
		{
			if(lights.empty() || lightIds.empty()) initBridge();
		} 
		else
		{
			restoreOriginalState();
			lights.clear();
		}
	}
	_isActive = newState;
}
