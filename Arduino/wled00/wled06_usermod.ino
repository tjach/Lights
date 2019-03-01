/*
 * This file allows you to add own functionality to WLED more easily
 * See: https://github.com/Aircoookie/WLED/wiki/Add-own-functionality
 * EEPROM bytes 2944 to 3071 are reserved for your custom use case.
 */

//Use userVar0 and userVar1 (API calls &U0=,&U1=, uint16_t)

#include <FS.h>
#include <NeoPixelBus.h>
#include <ArduinoJson.h>

struct state {
  uint8_t colors[3], bri = 100, sat = 254, colorMode = 2;
  bool lightState;
  int ct = 200, hue;
  float stepLevel[3], currentColors[3], x, y;
};

//core

#define entertainmentTimeout 1500 // millis

state lights[10];
bool inTransition, entertainmentRun, useDhcp = true;
byte mac[6], packetBuffer[46];
unsigned long lastEPMillis;

//settings
char *lightName = "Wled diyHue Strip";
uint8_t scene, startup, onPin = 14, offPin = 12;
bool hwSwitch = false;

uint8_t lightsCount = 4;
uint16_t pixelCount = ledCount, lightLedsCount;
uint8_t transitionLeds = 0; // must be even number

ESP8266HTTPUpdateServer httpUpdateServer;

RgbColor red = RgbColor(255, 0, 0);
RgbColor green = RgbColor(0, 255, 0);
RgbColor white = RgbColor(255);
RgbColor black = RgbColor(0);

NeoPixelBus<NeoGrbFeature, NeoEsp8266UartWs2813Method>* _pGrb = NULL;

void convertHue(uint8_t light)
{
  double      hh, p, q, t, ff, s, v;
  long        i;

  s = lights[light].sat / 255.0;
  v = lights[light].bri / 255.0;

  if (s <= 0.0) {      // < is bogus, just shuts up warnings
    lights[light].colors[0] = v;
    lights[light].colors[1] = v;
    lights[light].colors[2] = v;
    return;
  }
  hh = lights[light].hue;
  if (hh >= 65535.0) hh = 0.0;
  hh /= 11850, 0;
  i = (long)hh;
  ff = hh - i;
  p = v * (1.0 - s);
  q = v * (1.0 - (s * ff));
  t = v * (1.0 - (s * (1.0 - ff)));

  switch (i) {
    case 0:
      lights[light].colors[0] = v * 255.0;
      lights[light].colors[1] = t * 255.0;
      lights[light].colors[2] = p * 255.0;
      break;
    case 1:
      lights[light].colors[0] = q * 255.0;
      lights[light].colors[1] = v * 255.0;
      lights[light].colors[2] = p * 255.0;
      break;
    case 2:
      lights[light].colors[0] = p * 255.0;
      lights[light].colors[1] = v * 255.0;
      lights[light].colors[2] = t * 255.0;
      break;

    case 3:
      lights[light].colors[0] = p * 255.0;
      lights[light].colors[1] = q * 255.0;
      lights[light].colors[2] = v * 255.0;
      break;
    case 4:
      lights[light].colors[0] = t * 255.0;
      lights[light].colors[1] = p * 255.0;
      lights[light].colors[2] = v * 255.0;
      break;
    case 5:
    default:
      lights[light].colors[0] = v * 255.0;
      lights[light].colors[1] = p * 255.0;
      lights[light].colors[2] = q * 255.0;
      break;
  }

}

void convertXy(uint8_t light)
{
  int optimal_bri = lights[light].bri;
  if (optimal_bri < 5) {
    optimal_bri = 5;
  }
  float Y = lights[light].y;
  float X = lights[light].x;
  float Z = 1.0f - lights[light].x - lights[light].y;

  // sRGB D65 conversion
  float r =  X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
  float g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
  float b =  X * 0.0557f - Y * 0.2040f + Z * 1.0570f;


  // Apply gamma correction
  r = r <= 0.04045f ? r / 12.92f : pow((r + 0.055f) / (1.0f + 0.055f), 2.4f);
  g = g <= 0.04045f ? g / 12.92f : pow((g + 0.055f) / (1.0f + 0.055f), 2.4f);
  b = b <= 0.04045f ? b / 12.92f : pow((b + 0.055f) / (1.0f + 0.055f), 2.4f);

  if (r > b && r > g) {
    // red is biggest
    if (r > 1.0f) {
      g = g / r;
      b = b / r;
      r = 1.0f;
    }
  }
  else if (g > b && g > r) {
    // green is biggest
    if (g > 1.0f) {
      r = r / g;
      b = b / g;
      g = 1.0f;
    }
  }
  else if (b > r && b > g) {
    // blue is biggest
    if (b > 1.0f) {
      r = r / b;
      g = g / b;
      b = 1.0f;
    }
  }

  r = r < 0 ? 0 : r;
  g = g < 0 ? 0 : g;
  b = b < 0 ? 0 : b;

  lights[light].colors[0] = (int) (r * optimal_bri); lights[light].colors[1] = (int) (g * optimal_bri); lights[light].colors[2] = (int) (b * optimal_bri);
}

void convertCt(uint8_t light) {
  int hectemp = 10000 / lights[light].ct;
  int r, g, b;
  if (hectemp <= 66) {
    r = 255;
    g = 99.4708025861 * log(hectemp) - 161.1195681661;
    b = hectemp <= 19 ? 0 : (138.5177312231 * log(hectemp - 10) - 305.0447927307);
  } else {
    r = 329.698727446 * pow(hectemp - 60, -0.1332047592);
    g = 288.1221695283 * pow(hectemp - 60, -0.0755148492);
    b = 255;
  }
  r = r > 255 ? 255 : r;
  g = g > 255 ? 255 : g;
  b = b > 255 ? 255 : b;
  lights[light].colors[0] = r * (lights[light].bri / 255.0f); lights[light].colors[1] = g * (lights[light].bri / 255.0f); lights[light].colors[2] = b * (lights[light].bri / 255.0f);
}

void apply_scene(uint8_t new_scene) {
  for (uint8_t light = 0; light < lightsCount; light++) {
    if ( new_scene == 1) {
      lights[light].bri = 254; lights[light].ct = 346; lights[light].colorMode = 2; convertCt(light);
    } else if ( new_scene == 2) {
      lights[light].bri = 254; lights[light].ct = 233; lights[light].colorMode = 2; convertCt(light);
    }  else if ( new_scene == 3) {
      lights[light].bri = 254; lights[light].ct = 156; lights[light].colorMode = 2; convertCt(light);
    }  else if ( new_scene == 4) {
      lights[light].bri = 77; lights[light].ct = 367; lights[light].colorMode = 2; convertCt(light);
    }  else if ( new_scene == 5) {
      lights[light].bri = 254; lights[light].ct = 447; lights[light].colorMode = 2; convertCt(light);
    }  else if ( new_scene == 6) {
      lights[light].bri = 1; lights[light].x = 0.561; lights[light].y = 0.4042; lights[light].colorMode = 1; convertXy(light);
    }  else if ( new_scene == 7) {
      lights[light].bri = 203; lights[light].x = 0.380328; lights[light].y = 0.39986; lights[light].colorMode = 1; convertXy(light);
    }  else if ( new_scene == 8) {
      lights[light].bri = 112; lights[light].x = 0.359168; lights[light].y = 0.28807; lights[light].colorMode = 1; convertXy(light);
    }  else if ( new_scene == 9) {
      lights[light].bri = 142; lights[light].x = 0.267102; lights[light].y = 0.23755; lights[light].colorMode = 1; convertXy(light);
    }  else if ( new_scene == 10) {
      lights[light].bri = 216; lights[light].x = 0.393209; lights[light].y = 0.29961; lights[light].colorMode = 1; convertXy(light);
    } else {
      lights[light].bri = 144; lights[light].ct = 447; lights[light].colorMode = 2; convertCt(light);
    }
  }
}

void processLightdata(uint8_t light, float transitiontime) {
  transitiontime *= 17 - (pixelCount / 40); //every extra led add a small delay that need to be counted
  if (lights[light].colorMode == 1 && lights[light].lightState == true) {
    convertXy(light);
  } else if (lights[light].colorMode == 2 && lights[light].lightState == true) {
    convertCt(light);
  } else if (lights[light].colorMode == 3 && lights[light].lightState == true) {
    convertHue(light);
  }
  for (uint8_t i = 0; i < 3; i++) {
    if (lights[light].lightState) {
      lights[light].stepLevel[i] = ((float)lights[light].colors[i] - lights[light].currentColors[i]) / transitiontime;
    } else {
      lights[light].stepLevel[i] = lights[light].currentColors[i] / transitiontime;
    }
  }
}

RgbColor blending(float left[3], float right[3], uint8_t pixel) {
  uint8_t result[3];
  for (uint8_t i = 0; i < 3; i++) {
    float percent = (float) pixel / (float) (transitionLeds + 1);
    result[i] = (left[i] * (1.0f - percent) + right[i] * percent) / 2;
  }
  return RgbColor((uint8_t)result[0], (uint8_t)result[1], (uint8_t)result[2]);
}

RgbColor convInt(float color[3]) {
  return RgbColor((uint8_t)color[0], (uint8_t)color[1], (uint8_t)color[2]);
}

RgbColor convFloat(float color[3]) {
  return RgbColor((uint8_t)color[0], (uint8_t)color[1], (uint8_t)color[2]);
}

void lightEngine() {
  for (int light = 0; light < lightsCount; light++) {
    if (lights[light].lightState) {
      if (lights[light].colors[0] != lights[light].currentColors[0] || lights[light].colors[1] != lights[light].currentColors[1] || lights[light].colors[2] != lights[light].currentColors[2]) {
        inTransition = true;
        for (uint8_t k = 0; k < 3; k++) {
          if (lights[light].colors[k] != lights[light].currentColors[k]) lights[light].currentColors[k] += lights[light].stepLevel[k];
          if ((lights[light].stepLevel[k] > 0.0 && lights[light].currentColors[k] > lights[light].colors[k]) || (lights[light].stepLevel[k] < 0.0 && lights[light].currentColors[k] < lights[light].colors[k])) lights[light].currentColors[k] = lights[light].colors[k];
        }
        if (lightsCount > 1) {
          if (light == 0) {
            for (uint8_t pixel = 0; pixel < lightLedsCount + transitionLeds / 2; pixel++) {
              if (pixel < lightLedsCount - transitionLeds / 2) {
                _pGrb->SetPixelColor(pixel, convFloat(lights[light].currentColors));
              } else {
                _pGrb->SetPixelColor(pixel, blending(lights[0].currentColors, lights[1].currentColors, pixel + 1 - (lightLedsCount - transitionLeds / 2 )));
              }
            }
          } else if (light == lightsCount - 1) {
            for (uint8_t pixel = 0; pixel < lightLedsCount + transitionLeds / 2 ; pixel++) {
              if (pixel < transitionLeds) {
                _pGrb->SetPixelColor(pixel - transitionLeds / 2 + lightLedsCount * light, blending( lights[light - 1].currentColors, lights[light].currentColors, pixel + 1));
              } else {
                _pGrb->SetPixelColor(pixel - transitionLeds / 2 + lightLedsCount * light, convFloat(lights[light].currentColors));
              }
            }
          } else {
            for (uint8_t pixel = 0; pixel < lightLedsCount + transitionLeds; pixel++) {
              if (pixel < transitionLeds) {
                _pGrb->SetPixelColor(pixel - transitionLeds / 2 + lightLedsCount * light,  blending( lights[light - 1].currentColors, lights[light].currentColors, pixel + 1));
              } else if (pixel > lightLedsCount - 1) {
                _pGrb->SetPixelColor(pixel - transitionLeds / 2 + lightLedsCount * light,  blending( lights[light].currentColors, lights[light + 1].currentColors, pixel + 1 - lightLedsCount));
              } else  {
                _pGrb->SetPixelColor(pixel - transitionLeds / 2 + lightLedsCount * light, convFloat(lights[light].currentColors));
              }
            }
          }
        } else {
          _pGrb->ClearTo(convFloat(lights[light].currentColors), 0, pixelCount - 1);
        }
        _pGrb->Show();
      }
    } else {
      if (lights[light].currentColors[0] != 0 || lights[light].currentColors[1] != 0 || lights[light].currentColors[2] != 0) {
        inTransition = true;
        for (uint8_t k = 0; k < 3; k++) {
          if (lights[light].currentColors[k] != 0) lights[light].currentColors[k] -= lights[light].stepLevel[k];
          if (lights[light].currentColors[k] < 0) lights[light].currentColors[k] = 0;
        }
        if (lightsCount > 1) {
          if (light == 0) {
            for (uint8_t pixel = 0; pixel < lightLedsCount + transitionLeds / 2; pixel++) {
              if (pixel < lightLedsCount - transitionLeds / 2) {
                _pGrb->SetPixelColor(pixel, convFloat(lights[light].currentColors));
              } else {
                _pGrb->SetPixelColor(pixel,  blending( lights[light].currentColors, lights[light + 1].currentColors, pixel + 1 - (lightLedsCount - transitionLeds / 2 )));
              }
            }
          } else if (light == lightsCount - 1) {
            for (uint8_t pixel = 0; pixel < lightLedsCount + transitionLeds / 2 ; pixel++) {
              if (pixel < transitionLeds) {
                _pGrb->SetPixelColor(pixel - transitionLeds / 2 + lightLedsCount * light,  blending( lights[light - 1].currentColors, lights[light].currentColors, pixel + 1));
              } else {
                _pGrb->SetPixelColor(pixel - transitionLeds / 2 + lightLedsCount * light, convFloat(lights[light].currentColors));
              }
            }
          } else {
            for (uint8_t pixel = 0; pixel < lightLedsCount + transitionLeds; pixel++) {
              if (pixel < transitionLeds) {
                _pGrb->SetPixelColor(pixel - transitionLeds / 2 + lightLedsCount * light,  blending( lights[light - 1].currentColors, lights[light].currentColors, pixel + 1));
              } else if (pixel > lightLedsCount - 1) {
                _pGrb->SetPixelColor(pixel - transitionLeds / 2 + lightLedsCount * light,  blending( lights[light].currentColors, lights[light + 1].currentColors, pixel + 1 - lightLedsCount));
              } else  {
                _pGrb->SetPixelColor(pixel - transitionLeds / 2 + lightLedsCount * light, convFloat(lights[light].currentColors));
              }
            }
          }
        } else {
          _pGrb->ClearTo(convFloat(lights[light].currentColors), 0, pixelCount - 1);
        }
        _pGrb->Show();
      }
    }
  }
  if (inTransition) {
    delay(6);
    inTransition = false;
  } else if (hwSwitch == true) {
    if (digitalRead(onPin) == HIGH) {
      int i = 0;
      while (digitalRead(onPin) == HIGH && i < 30) {
        delay(20);
        i++;
      }
      for (int light = 0; light < lightsCount; light++) {
        if (i < 30) {
          // there was a short press
          lights[light].lightState = true;
        }
        else {
          // there was a long press
          lights[light].bri += 56;
          if (lights[light].bri > 255) {
            // don't increase the brightness more then maximum value
            lights[light].bri = 255;
          }
        }
      }
    } else if (digitalRead(offPin) == HIGH) {
      int i = 0;
      while (digitalRead(offPin) == HIGH && i < 30) {
        delay(20);
        i++;
      }
      for (int light = 0; light < lightsCount; light++) {
        if (i < 30) {
          // there was a short press
          lights[light].lightState = false;
        }
        else {
          // there was a long press
          lights[light].bri -= 56;
          if (lights[light].bri < 1) {
            // don't decrease the brightness less than minimum value.
            lights[light].bri = 1;
          }
        }
      }
    }
  }
}

void saveState() {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  for (uint8_t i = 0; i < lightsCount; i++) {
    JsonObject& light = json.createNestedObject((String)i);
    light["on"] = lights[i].lightState;
    light["bri"] = lights[i].bri;
    if (lights[i].colorMode == 1) {
      light["x"] = lights[i].x;
      light["y"] = lights[i].y;
    } else if (lights[i].colorMode == 2) {
      light["ct"] = lights[i].ct;
    } else if (lights[i].colorMode == 3) {
      light["hue"] = lights[i].hue;
      light["sat"] = lights[i].sat;
    }
  }
  File stateFile = SPIFFS.open("/state.json", "w");
  json.printTo(stateFile);

}


void restoreState() {
  File stateFile = SPIFFS.open("/state.json", "r");
  if (!stateFile) {
    saveState();
    return;
  }

  size_t size = stateFile.size();

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  stateFile.readBytes(buf.get(), size);

  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    //Serial.println("Failed to parse config file");
    return;
  }
  for (JsonPair& state : json) {
    const char* key = state.key;
    int lightId = atoi(key);
    JsonObject& values = state.value;
    lights[lightId].lightState = values["on"];
    lights[lightId].bri = (uint8_t)values["bri"];
    if (values.containsKey("x")) {
      lights[lightId].x = values["x"];
      lights[lightId].y = values["y"];
      lights[lightId].colorMode = 1;
    } else if (values.containsKey("ct")) {
      lights[lightId].ct = values["ct"];
      lights[lightId].colorMode = 2;
    } else {
      if (values.containsKey("hue")) {
        lights[lightId].hue = values["hue"];
        lights[lightId].colorMode = 3;
      }
      if (values.containsKey("sat")) {
        lights[lightId].sat = (uint8_t) values["sat"];
        lights[lightId].colorMode = 3;
      }
    }
  }
}


bool saveConfig() {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["name"] = lightName;
  json["startup"] = startup;
  json["scene"] = scene;
  json["on"] = onPin;
  json["off"] = offPin;
  json["hw"] = hwSwitch;
  json["dhcp"] = useDhcp;
  json["lightsCount"] = lightsCount;
  json["pixelCount"] = pixelCount;
  json["transLeds"] = transitionLeds;
  JsonArray& addr = json.createNestedArray("addr");
  addr.add(staticIP[0]);
  addr.add(staticIP[1]);
  addr.add(staticIP[2]);
  addr.add(staticIP[3]);
  JsonArray& gw = json.createNestedArray("gw");
  gw.add(staticGateway[0]);
  gw.add(staticGateway[1]);
  gw.add(staticGateway[2]);
  gw.add(staticGateway[3]);
  JsonArray& mask = json.createNestedArray("mask");
  mask.add(staticSubnet[0]);
  mask.add(staticSubnet[1]);
  mask.add(staticSubnet[2]);
  mask.add(staticSubnet[3]);
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    //Serial.println("Failed to open config file for writing");
    return false;
  }

  json.printTo(configFile);
  return true;
}

bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    //Serial.println("Create new file with default values");
    return saveConfig();
  }

  size_t size = configFile.size();
  if (size > 1024) {
    //Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  //Serial.println(buf.get());

  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    //Serial.println("Failed to parse config file");
    return false;
  }

  strcpy(lightName, json["name"]);
  startup = (uint8_t) json["startup"];
  scene  = (uint8_t) json["scene"];
  onPin = (uint8_t) json["on"];
  offPin = (uint8_t) json["off"];
  hwSwitch = json["hw"];
  lightsCount = (uint16_t) json["lightsCount"];
  pixelCount = (uint16_t) json["pixelCount"];
  transitionLeds = (uint8_t) json["transLeds"];
  useDhcp = json["dhcp"];
  staticIP = {json["addr"][0], json["addr"][1], json["addr"][2], json["addr"][3]};
  staticSubnet = {json["mask"][0], json["mask"][1], json["mask"][2], json["mask"][3]};
  staticGateway = {json["gw"][0], json["gw"][1], json["gw"][2], json["gw"][3]};
  return true;
}

void ChangeNeoPixels(uint16_t newCount)
{
  if (_pGrb != NULL) {
    delete _pGrb; // delete the previous dynamically created strip
  }
  _pGrb = new NeoPixelBus<NeoGrbFeature, NeoEsp8266UartWs2813Method>(newCount); // and recreate with new count
  _pGrb->Begin();
}

void userBeginPreConnection()
{
  
}

void userBegin()
{
  //Serial.begin(115200);
  //Serial.println();
  delay(1000);

  //Serial.println("mounting FS...");

  if (!SPIFFS.begin()) {
    //Serial.println("Failed to mount file system");
    return;
  }

  if (!loadConfig()) {
    //Serial.println("Failed to load config");
  } else {
    ////Serial.println("Config loaded");
  }

  lightLedsCount = pixelCount / lightsCount;
  ChangeNeoPixels(pixelCount);


  if (startup == 1) {
    for (uint8_t i = 0; i < lightsCount; i++) {
      lights[i].lightState = true;
    }
  }
  if (startup == 0) {
    restoreState();
  } else {
    apply_scene(scene);
  }
  for (uint8_t i = 0; i < lightsCount; i++) {
    processLightdata(i, 4);
  }
  if (lights[0].lightState) {
    for (uint8_t i = 0; i < 200; i++) {
      lightEngine();
    }
  }


  if (useDhcp) {
    staticIP = WiFi.localIP();
    staticGateway = WiFi.gatewayIP();
    staticSubnet = WiFi.subnetMask();
  }

  WiFi.macAddress(mac);

  httpUpdateServer.setup(&server);

  Udp.begin(2100);

  if (hwSwitch == true) {
    pinMode(onPin, INPUT);
    pinMode(offPin, INPUT);
  }

  server.on("/state", HTTP_PUT, []() {
    bool stateSave = false;
    DynamicJsonBuffer newBuffer;
    JsonObject& root = newBuffer.parseObject(server.arg("plain"));
    if (!root.success()) {
      server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
    } else {
      for (JsonPair& state : root) {
        const char* key = state.key;
        int light = atoi(key) - 1;
        JsonObject& values = state.value;
        int transitiontime = 4;

        if (values.containsKey("xy")) {
          lights[light].x = values["xy"][0];
          lights[light].y = values["xy"][1];
          lights[light].colorMode = 1;
        } else if (values.containsKey("ct")) {
          lights[light].ct = values["ct"];
          lights[light].colorMode = 2;
        } else {
          if (values.containsKey("hue")) {
            lights[light].hue = values["hue"];
            lights[light].colorMode = 3;
          }
          if (values.containsKey("sat")) {
            lights[light].sat = values["sat"];
            lights[light].colorMode = 3;
          }
        }

        if (values.containsKey("on")) {
          if (values["on"]) {
            lights[light].lightState = true;
          } else {
            lights[light].lightState = false;
          }
          if (startup == 0) {
            stateSave = true;
          }
        }

        if (values.containsKey("bri")) {
          lights[light].bri = values["bri"];
        }

        if (values.containsKey("bri_inc")) {
          lights[light].bri += (int) values["bri_inc"];
          if (lights[light].bri > 255) lights[light].bri = 255;
          else if (lights[light].bri < 1) lights[light].bri = 1;
        }

        if (values.containsKey("transitiontime")) {
          transitiontime = values["transitiontime"];
        }

        if (values.containsKey("alert") && values["alert"] == "select") {
          if (lights[light].lightState) {
            lights[light].currentColors[0] = 0; lights[light].currentColors[1] = 0; lights[light].currentColors[2] = 0; lights[light].currentColors[3] = 0;
          } else {
            lights[light].currentColors[3] = 126; lights[light].currentColors[4] = 126;
          }
        }
        processLightdata(light, transitiontime);
      }
      String output;
      root.printTo(output);
      server.send(200, "text/plain", output);
      if (stateSave) {
        saveState();
      }
    }
  });

  server.on("/state", HTTP_GET, []() {
    uint8_t light = server.arg("light").toInt() - 1;
    DynamicJsonBuffer newBuffer;
    JsonObject& root = newBuffer.createObject();

    root["on"] = lights[light].lightState;
    root["bri"] = lights[light].bri;
    JsonArray& xy = root.createNestedArray("xy");
    xy.add(lights[light].x);
    xy.add(lights[light].y);
    root["ct"] = lights[light].ct;
    root["hue"] = lights[light].hue;
    root["sat"] = lights[light].sat;
    if (lights[light].colorMode == 1)
      root["colormode"] = "xy";
    else if (lights[light].colorMode == 2)
      root["colormode"] = "ct";
    else if (lights[light].colorMode == 3)
      root["colormode"] = "hs";
    String output;
    root.printTo(output);
    server.send(200, "text/plain", output);
  });

  server.on("/detect", []() {
    char macString[32] = {0};
    sprintf(macString, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    DynamicJsonBuffer newBuffer;
    JsonObject& root = newBuffer.createObject();
    root["name"] = lightName;
    root["lights"] = lightsCount;
    root["protocol"] = "native_multi";
    root["modelid"] = "LST002";
    root["type"] = "ws2812_strip";
    root["mac"] = String(macString);
    root["version"] = 2.0;
    String output;
    root.printTo(output);
    server.send(200, "text/plain", output);
  });

  server.on("/config", []() {
    DynamicJsonBuffer newBuffer;
    JsonObject& root = newBuffer.createObject();
    root["name"] = lightName;
    root["scene"] = scene;
    root["startup"] = startup;
    root["hw"] = hwSwitch;
    root["on"] = onPin;
    root["off"] = offPin;
    root["hwswitch"] = (int)hwSwitch;
    root["lightscount"] = lightsCount;
    root["pixelcount"] = pixelCount;
    root["transitionleds"] = transitionLeds;
    root["dhcp"] = (int)useDhcp;
    root["addr"] = (String)staticIP[0] + "." + (String)staticIP[1] + "." + (String)staticIP[2] + "." + (String)staticIP[3];
    root["gw"] = (String)staticGateway[0] + "." + (String)staticGateway[1] + "." + (String)staticGateway[2] + "." + (String)staticGateway[3];
    root["sm"] = (String)staticSubnet[0] + "." + (String)staticSubnet[1] + "." + (String)staticSubnet[2] + "." + (String)staticSubnet[3];
    String output;
    root.printTo(output);
    server.send(200, "text/plain", output);
  });

  server.begin();
}


RgbColor blendingEntert(float left[3], float right[3], float pixel) {
  uint8_t result[3];
  for (uint8_t i = 0; i < 3; i++) {
    float percent = (float) pixel / (float) (transitionLeds + 1);
    result[i] = (left[i] * (1.0f - percent) + right[i] * percent) / 2;
  }
  return RgbColor((uint8_t)result[0], (uint8_t)result[1], (uint8_t)result[2]);
}

void entertainment() {
  uint8_t packetSize = Udp.parsePacket();
  if (packetSize) {
    if (!entertainmentRun) {
      entertainmentRun = true;
    }
    lastEPMillis = millis();
    Udp.read(packetBuffer, packetSize);
    for (uint8_t i = 0; i < packetSize / 4; i++) {
      lights[packetBuffer[i * 4]].currentColors[0] = packetBuffer[i * 4 + 1];
      lights[packetBuffer[i * 4]].currentColors[1] = packetBuffer[i * 4 + 2];
      lights[packetBuffer[i * 4]].currentColors[2] = packetBuffer[i * 4 + 3];
    }
    for (uint8_t light = 0; light < lightsCount; light++) {
      if (lightsCount > 1) {
        if (light == 0) {
          for (uint8_t pixel = 0; pixel < lightLedsCount + transitionLeds / 2; pixel++) {
            if (pixel < lightLedsCount - transitionLeds / 2) {
              _pGrb->SetPixelColor(pixel, convInt(lights[light].currentColors));
            } else {
              _pGrb->SetPixelColor(pixel, blendingEntert(lights[0].currentColors, lights[1].currentColors, pixel + 1 - (lightLedsCount - transitionLeds / 2 )));
            }
          }
        } else if (light == lightsCount - 1) {
          for (uint8_t pixel = 0; pixel < lightLedsCount - transitionLeds / 2 ; pixel++) {
            _pGrb->SetPixelColor(pixel + transitionLeds / 2 + lightLedsCount * light, convInt(lights[light].currentColors));
          }
        } else {
          for (uint8_t pixel = 0; pixel < lightLedsCount; pixel++) {
            if (pixel < lightLedsCount - transitionLeds) {
              _pGrb->SetPixelColor(pixel + transitionLeds / 2 + lightLedsCount * light, convInt(lights[light].currentColors));
            } else {
              _pGrb->SetPixelColor(pixel + transitionLeds / 2 + lightLedsCount * light, blendingEntert(lights[light].currentColors, lights[light + 1].currentColors, pixel - (lightLedsCount - transitionLeds ) + 1));
            }
          }
        }
      } else {
        _pGrb->ClearTo(RgbColor(lights[0].colors[0], lights[0].colors[1], lights[0].colors[2]), 0, lightLedsCount - 1);
      }
    }
    _pGrb->Show();
  }
}

void userLoop()
{
  server.handleClient();
  if (!entertainmentRun) {
    lightEngine();
  } else {
    if ((millis() - lastEPMillis) >= entertainmentTimeout) {
      entertainmentRun = false;
      for (uint8_t i = 0; i < lightsCount; i++) {
        processLightdata(i, 4); //return to original colors with 0.4 sec transition
      }
    }
  }
  entertainment();
}
