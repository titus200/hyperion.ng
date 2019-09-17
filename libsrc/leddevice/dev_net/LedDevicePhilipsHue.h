#pragma once

// Leddevice includes
#include <leddevice/LedDevice.h>

// STL includes
#include <set>

// Qt includes
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QDebug>
#include <QThread>

// Forward declaration
struct CiColorTriangle;

/**
 * A color point in the color space of the hue system.
 */
struct CiColor
{
	/// X component.
	double x;
	/// Y component.
	double y;
	/// The brightness.
	double bri;

	///
	/// Converts an RGB color to the Hue xy color space and brightness.
	/// https://github.com/PhilipsHue/PhilipsHueSDK-iOS-OSX/blob/master/ApplicationDesignNotes/RGB%20to%20xy%20Color%20conversion.md
	///
	/// @param red the red component in [0, 1]
	///
	/// @param green the green component in [0, 1]
	///
	/// @param blue the blue component in [0, 1]
	///
	/// @return color point
	///
	static CiColor rgbToCiColor(double red, double green, double blue, CiColorTriangle colorSpace);

	///
	/// @param p the color point to check
	///
	/// @return true if the color point is covered by the lamp color space
	///
	static bool isPointInLampsReach(CiColor p, CiColorTriangle colorSpace);

	///
	/// @param p1 point one
	///
	/// @param p2 point tow
	///
	/// @return the cross product between p1 and p2
	///
	static double crossProduct(CiColor p1, CiColor p2);

	///
	/// @param a reference point one
	///
	/// @param b reference point two
	///
	/// @param p the point to which the closest point is to be found
	///
	/// @return the closest color point of p to a and b
	///
	static CiColor getClosestPointToPoint(CiColor a, CiColor b, CiColor p);

	///
	/// @param p1 point one
	///
	/// @param p2 point tow
	///
	/// @return the distance between the two points
	///
	static double getDistanceBetweenTwoPoints(CiColor p1, CiColor p2);
};

bool operator==(CiColor p1, CiColor p2);
bool operator!=(CiColor p1, CiColor p2);

/**
 * Color triangle to define an available color space for the hue lamps.
 */
struct CiColorTriangle
{
	CiColor red, green, blue;
};

/**
 * Simple class to hold the id, the latest color, the color space and the original state.
 */
class PhilipsHueLight
{

private:
	Logger* _log;
	/// light id
	unsigned int id;
	unsigned int ledidx;
	bool on;
	unsigned int transitionTime;
	bool logCommands;
	CiColor color;
	/// darkes blue color in hue lamp GAMUT = black
	CiColor colorBlack;
	/// The model id of the hue lamp which is used to determine the color space.
	QString modelId;
	QString lightname;
	CiColorTriangle colorSpace;
	/// The json string of the original state.
	QString originalState;
	CiColor originalColor;
	///
	/// @param state the state as json object to set
	///
	void set(QString state);

public:
	QJsonObject originalStateJSON;

	// Hue system model ids (http://www.developers.meethue.com/documentation/supported-lights).
	// Light strips, color iris, ...
	static const std::set<QString> GAMUT_A_MODEL_IDS;
	// Hue bulbs, spots, ...
	static const std::set<QString> GAMUT_B_MODEL_IDS;
	// Hue Lightstrip plus, go ...
	static const std::set<QString> GAMUT_C_MODEL_IDS;

	///
	/// Constructs the light.
	///
	/// @param log the logger
	/// @param bridge the bridge
	/// @param id the light id
	///
	PhilipsHueLight(Logger* log, unsigned int id, QJsonObject values, unsigned int ledidx);
	~PhilipsHueLight();

	unsigned int getId() const;
	QString getOriginalState();

	void saveOriginalState(QJsonObject values);
	///
	/// @param on
	///
	void setOn(bool on);
	///
	/// @param transitionTime the transition time between colors in multiples of 100 ms
	///
	void setTransitionTime(unsigned int transitionTime);

	///
	/// @param color the color to set
	/// @param brightnessFactor the factor to apply to the CiColor#bri value
	///
	void setColor(CiColor color, bool isStream = false, double brightnessFactor = 1.0, double brightnessMin = 0.0, double brightnessMax = 1.0);
	CiColor getColor() const;

	///
	/// @return the color space of the light determined by the model id reported by the bridge.
	CiColorTriangle getColorSpace() const;
};

class PhilipsHueBridge : public LedDevice
{
	Q_OBJECT

private:
	/// QNetworkAccessManager for sending requests.
	QNetworkAccessManager* _manager;
	/// Timer for bridge reconnect interval
	QTimer bTimer;

private slots:
	///
	/// Receive all replies and check for error, schedule reconnect on issues
	/// Emits newLights() on success when triggered from connect()
	///
	void resolveReply(QNetworkReply* const &reply);

public slots:
	/// thread start
	virtual void start();

	///
	/// Connect to bridge to check availbility and user
	///
	void bridgeConnect(void);
	///
	/// Set streaming group active true / false
	///
	void getStreamGroupState(bool startStreaming = true);
	void setStreamGroupState(bool state = false, bool startStreaming = true);
	void setLightState(unsigned int lightId = 0, QString state = "");

//signals:
	///
	///	Emits with a QMap of current bridge light/value pairs
	///
	virtual void newLights(QMap<quint16,QJsonObject> map) {}
	///
	///	Emits with a QMap of current bridge light/value pairs
	///
	virtual void newGroups(QMap<quint16,QJsonObject> map) {}
	///
	///	Emits with a QMap of current bridge light/value pairs
	///
	virtual void updateLights(QMap<quint16,QJsonObject> map) {}

	virtual void restoreOriginalState() {}

signals:
	//void prepareStreamGroup();

	void buildStreamer();

public:
	PhilipsHueBridge(const QJsonObject &deviceConfig);
	~PhilipsHueBridge();

	/// Ip address of the bridge
	QString address;
	/// User name for the API ("newdeveloper")
	QString username;
	/// Clientkey
	QString clientkey;
	/// used groupId;
	unsigned int groupId;

	bool onlyUpdates;
	bool checkGroupStreamState;
	bool logCommands;
	bool startStreaming;
	unsigned int transitionTime;

	///
	/// @param method the method of the request.
	///
	/// @param route the route of the method request.
	///
	/// @param content the content of the method request.
	///
	void sendRequest(QString method = "", QString route = "", QString content = "");
};

/**
 * Implementation for the Philips Hue system.
 *
 * To use set the device to "philipshue".
 * Uses the official Philips Hue API (http://developers.meethue.com).
 *
 * @author ntim (github), bimsarck (github)
 */
class LedDevicePhilipsHue: public PhilipsHueBridge
{
	Q_OBJECT

public:
	///
	/// Constructs specific LedDevice
	///
	/// @param deviceConfig json device config
	///
	LedDevicePhilipsHue(const QJsonObject &deviceConfig);

	///
	/// Destructor of this device
	///
	~LedDevicePhilipsHue();

	/// constructs leddevice
	static LedDevice* construct(const QJsonObject &deviceConfig);

public slots:
	/// thread start
	virtual void start();
	/// creates new PhilipsHueLight(s) based on user lightid with bridge feedback
	///
	/// @param map Map of lightid/value pairs of bridge
	///
	void newLights(QMap<quint16, QJsonObject> map);

	void restoreOriginalState();

private:
	/// bridge class
	// PhilipsHueBridge* _bridge;
	void initBridge();
	bool _isActive;

	bool switchOffOnBlack;
	/// The brightness factor to multiply on color change.
	double brightnessFactor;
	/// The min brightness value.
	double brightnessMin;
	/// The max brightness value.
	double brightnessMax;
	/// Transition time in multiples of 100 ms.
	/// The default of the Hue lights is 400 ms, but we may want it snapier.
	unsigned int transitionTime;
	/// Array of the light ids.
	std::vector<unsigned int> lightIds;
	/// Array to save the lamps.
	std::vector<PhilipsHueLight> lights;

private slots:
	void stateChanged(bool newState);

protected:
	///
	/// Writes the RGB-Color values to the leds.
	///
	/// @param[in] ledValues  The RGB-color per led
	///
	/// @return Zero on success else negative
	///
	int write(const std::vector<ColorRgb> &ledValues);
	bool init(const QJsonObject &deviceConfig);
};
