#include "FanCoilHelper.h"
#include "KMPCommon.h"
#include <ArduinoJson.h>          // Install with Library Manager. "ArduinoJson by Benoit Blanchon" https://github.com/bblanchon/ArduinoJson

SensorData TemperatureData;
float TempCollection[TEMPERATURE_ARRAY_LEN];

SensorData HumidityData;
float HumidityCollection[HUMIDITY_ARRAY_LEN];

SensorData InletData;
float InletCollection[INLET_ARRAY_LEN];

bool _shouldSaveConfig = false;

/**
* @brief Connect to WiFi access point.
*
* @return bool true - success.
*/
bool connectWiFi()
{
	if (WiFi.status() != WL_CONNECTED)
	{
		DEBUG_FC_PRINT(F("Connecting ["));
		DEBUG_FC_PRINT(WiFi.SSID());
		DEBUG_FC_PRINTLN(F("]..."));

		WiFi.begin();

		if (WiFi.waitForConnectResult() != WL_CONNECTED)
		{
			return false;
		}

		DEBUG_FC_PRINT(F("IP address: "));
		DEBUG_FC_PRINTLN(WiFi.localIP());
	}

	return true;
}

float calcAverage(float * data, uint8 dataLength, uint8 precision)
{
	// Get average value.
	double temp = 0.0;
	for (uint i = 0; i < dataLength; i++)
	{
		temp += data[i];
	}

	temp /= dataLength;

	float result = roundF(temp, precision);

	return result;
}

/**
* @brief Print debug information about topic and payload.
* @operationName Operation which send this data.
* @param topic The topic name.
* @param payload The payload data.
* @param length A payload length.
*
* @return void
*/
#ifdef WIFIFCMM_DEBUG
void printTopicAndPayload(const char* operationName, const char* topic, char* payload, unsigned int length)
{
	DEBUG_FC_PRINT(operationName);
	DEBUG_FC_PRINT(" topic [");
	DEBUG_FC_PRINT(topic);
	DEBUG_FC_PRINT("] payload [");
	for (uint i = 0; i < length; i++)
	{
		DEBUG_FC_PRINT((char)payload[i]);
	}
	DEBUG_FC_PRINTLN("]");
}
#endif

void copyJsonValue(char* s, const char* jsonValue)
{
	if (jsonValue == NULL)
	{
		s[0] = '\0';
	}

	size_t len = strlen(jsonValue);

	strncpy(s, jsonValue, len);

	s[len] = '\0';
}

void ReadConfiguration(DeviceSettings* settings)
{
	if (!SPIFFS.begin())
	{
		DEBUG_FC_PRINTLN("Failed to mount FS");
	}
	else
	{
		DEBUG_FC_PRINTLN("The file system is mounted.");

		if (SPIFFS.exists(CONFIG_FILE_NAME))
		{
			//file exists, reading and loading
			DEBUG_FC_PRINTLN("Reading configuration file");
			File configFile = SPIFFS.open(CONFIG_FILE_NAME, "r");
			if (configFile)
			{
				DEBUG_FC_PRINTLN("Opening configuration file");
				size_t size = configFile.size();
				// Allocate a buffer to store contents of the file.
				std::unique_ptr<char[]> buf(new char[size]);

				configFile.readBytes(buf.get(), size);
				DynamicJsonBuffer jsonBuffer;
				JsonObject& json = jsonBuffer.parseObject(buf.get());
#ifdef WIFIFCMM_DEBUG
				json.printTo(DEBUG_FC);
#endif
				if (json.success())
				{
					DEBUG_FC_PRINTLN("\nJson is parsed");

					copyJsonValue(settings->MqttServer, json[MQTT_SERVER_KEY]);
					copyJsonValue(settings->MqttPort, json[MQTT_PORT_KEY]);
					copyJsonValue(settings->MqttClientId, json[MQTT_CLIENT_ID_KEY]);
					copyJsonValue(settings->MqttUser, json[MQTT_USER_KEY]);
					copyJsonValue(settings->MqttPass, json[MQTT_PASS_KEY]);
					copyJsonValue(settings->BaseTopic, json[BASE_TOPIC_KEY]);
					// After start device we should set this settings.
					//_mode = (Mode)atoi(json[MODE_KEY]);
					//_deviceState = atoi(json[TOPIC_DEVICE_STATE]) == 1 ? On : Off;
				}
				else
				{
					DEBUG_FC_PRINTLN(F("Loading json configuration is failed"));
				}
			}
		}
	}
}

/**
* @brief Setting information for connect WiFi and MQTT server. After successful connected this method save them.
* @param wifiManager.
*
* @return bool if successful connected - true else false.
*/
bool mangeConnectAndSettings(WiFiManager* wifiManager, DeviceSettings* settings)
{
	//read configuration from FS json
	DEBUG_FC_PRINTLN("Mounting FS...");

	ReadConfiguration(settings);

	// The extra parameters to be configured (can be either global or just in the setup)
	// After connecting, parameter.getValue() will get you the configured value
	// id/name placeholder/prompt default length
	WiFiManagerParameter customMqttServer("server", "MQTT server", settings->MqttServer, MQTT_SERVER_LEN);
	WiFiManagerParameter customMqttPort("port", "MQTT port", settings->MqttPort, MQTT_PORT_LEN);
	WiFiManagerParameter customClientName("clientName", "Client name", settings->MqttClientId, MQTT_CLIENT_ID_LEN);
	WiFiManagerParameter customMqttUser("user", "MQTT user", settings->MqttUser, MQTT_USER_LEN);
	WiFiManagerParameter customMqttPass("password", "MQTT pass", settings->MqttPass, MQTT_PASS_LEN);
	WiFiManagerParameter customBaseTopic("baseTopic", "Main topic", settings->BaseTopic, BASE_TOPIC_LEN);

	// add all your parameters here
	wifiManager->addParameter(&customMqttServer);
	wifiManager->addParameter(&customMqttPort);
	wifiManager->addParameter(&customClientName);
	wifiManager->addParameter(&customMqttUser);
	wifiManager->addParameter(&customMqttPass);
	wifiManager->addParameter(&customBaseTopic);

	DEBUG_FC_PRINTLN(F("Waiting WiFi up..."));

	// If the Fan coil (device) starts together with WiFi, need time to initialize WiFi router.
	// During this time (60 seconds) device trying to connect to WiFi.
	wifiManager->setTimeout(60);

	// fetches ssid and pass from eeprom and tries to connect
	// if it does not connect it starts an access point with the specified name
	// auto generated name ESP + ChipID
	if (!wifiManager->autoConnect())
	{
		DEBUG_FC_PRINTLN(F("Failed to connect and hit timeout"));
		return false;
	}

	//if you get here you have connected to the WiFi
	DEBUG_FC_PRINTLN("Connected.");

	if (_shouldSaveConfig)
	{
		//read updated parameters
		strcpy(settings->MqttServer, customMqttServer.getValue());
		strcpy(settings->MqttPort, customMqttPort.getValue());
		strcpy(settings->MqttClientId, customClientName.getValue());
		strcpy(settings->MqttUser, customMqttUser.getValue());
		strcpy(settings->MqttPass, customMqttPass.getValue());
		strcpy(settings->BaseTopic, customBaseTopic.getValue());

		SaveConfiguration(settings);
	}

	return true;
}

void SaveConfiguration(DeviceSettings* settings)
{
	DEBUG_FC_PRINTLN(F("Saving configuration..."));

	DynamicJsonBuffer jsonBuffer;
	JsonObject& json = jsonBuffer.createObject();

	json[MQTT_SERVER_KEY] = settings->MqttServer;
	json[MQTT_PORT_KEY] = settings->MqttPort;
	json[MQTT_CLIENT_ID_KEY] = settings->MqttClientId;
	json[MQTT_USER_KEY] = settings->MqttUser;
	json[MQTT_PASS_KEY] = settings->MqttPass;
	json[BASE_TOPIC_KEY] = settings->BaseTopic;

	// We shouldn't save this settings.
	//json[MODE_KEY] = (int)_mode;
	//json[TOPIC_DEVICE_STATE] = _deviceState == On ? 1 : 0;

	File configFile = SPIFFS.open(CONFIG_FILE_NAME, "w");
	if (!configFile) {
		DEBUG_FC_PRINTLN(F("Failed to open a configuration file for writing."));
	}
	else
	{
		DEBUG_FC_PRINTLN(F("Configuration is saved."));
	}

#ifdef WIFIFCMM_DEBUG
	json.prettyPrintTo(DEBUG_FC);
#endif

	json.printTo(configFile);
	configFile.close();
}

/**
* @brief Callback notifying us of the need to save configuration set from WiFiManager.
*
* @return void.
*/
void saveConfigCallback()
{
	DEBUG_FC_PRINTLN("Should save configuration");
	_shouldSaveConfig = true;
}

void setArrayValues(SensorData * sensor)
{
	if (!sensor->IsExists)
	{
		return;
	}
	
	for (size_t i = 0; i < sensor->DataCollectionLen; i++)
	{
		sensor->DataCollection[i] = sensor->Current;
	}

	sensor->Average = sensor->Current;
}

void initializeSensorData()
{
	TemperatureData.DataCollection = TempCollection;
	TemperatureData.DataCollectionLen = TEMPERATURE_ARRAY_LEN;
	TemperatureData.Precision = TEMPERATURE_PRECISION;
	TemperatureData.CheckDataIntervalMS = CHECK_TEMP_INTERVAL_MS;
	TemperatureData.DataType = Temperature;

	HumidityData.DataCollection = HumidityCollection;
	HumidityData.DataCollectionLen = HUMIDITY_ARRAY_LEN;
	HumidityData.Precision = HUMIDITY_PRECISION;
	HumidityData.CheckDataIntervalMS = CHECK_HUMIDITY_INTERVAL_MS;
	HumidityData.DataType = Humidity;

	InletData.DataCollection = InletCollection;
	InletData.DataCollectionLen = INLET_ARRAY_LEN;
	InletData.Precision = INLET_PRECISION;
	InletData.CheckDataIntervalMS = CHECK_INLET_INTERVAL_MS;
	InletData.DataType = InletPipe;
}