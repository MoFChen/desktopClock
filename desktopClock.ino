#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <FS.h>
#include <Ticker.h>
#include <Wire.h>

#include <DHT.h>                            // https://github.com/adafruit/DHT-sensor-library
#include <SSD1306Wire.h>                    // https://github.com/MoFChen/esp8266-oled-ssd1306
#include <OLEDDisplayUi.h>                  // https://github.com/MoFChen/esp8266-oled-ssd1306

#include <ArduinoJson.h>                    // https://github.com/bblanchon/ArduinoJson
#include <ESPAsyncTCP.h>                    // https://github.com/me-no-dev/ESPAsyncTCP
#include <AsyncHTTPRequest_Generic.h>       // https://github.com/khoih-prog/AsyncHTTPRequest_Generic
#include <AsyncMqtt_Generic.h>              // https://github.com/khoih-prog/AsyncMQTT_Generic

#include "desktopClock.h"
#include "assets.h"

#define LOG             Serial
#define DEBUG_ESP_PORT  Serial

#pragma region Global Variables

/* 文件系统标志 */
bool        flagFS = false;
/* 初始化标志 */
bool        initFlag = false;
/* 自动升级 */
bool        autoUpgrade = false;
/* DHT11温度 */
float       dht11Temp = 0.0;
/* DHT11湿度 */
float       dht11Humi = 0.0;
/* 按钮标志 */
uint8_t     buttonFlag = 0;

/* 网络时间API */
String      apiTimeUrl = API_TIME_URL;
/* 天气信息API */
String      apiWeatherUrl = API_WEATHER_URL;
/* 天气信息API Key */
String      apiWeatherKey = API_WEATHER_KEY;
/* 固件升级地址 */
String      upgradeUrl;
/* 固件更新版本 */
uint32_t    upgradeVersion = 0;

/* 时钟定时器 */
Ticker      tickerClock;
/* LED控制定时器 */
Ticker      tickerLED;
/* 感应定时器 */
Ticker      tickerSR602;
/* 网络时间同步定时器 */
Ticker      tickerSyncClock;
/* 天气信息同步计时器 */
Ticker      tickerSyncWeather;
/* 温湿度数据同步计时器 */
Ticker      tickerSyncData;
/* 重新连接计时器 */
Ticker      tickerReconnect;
/* 屏幕息屏计时器 */
Ticker      tickerScreenLock;
/* 屏幕滚动计时器 */
Ticker      tickerScreenScroll;

/* 网络时间HTTP请求类 */
AsyncHTTPRequest    requestTime;
/* 天气信息HTTP请求类 */
AsyncHTTPRequest    requestWeather;
/* MQTT 客户端类 */
AsyncMqttClient     MQTT;
/* JSON 解析 */
DynamicJsonDocument jsonDoc(1024);
/* DHT11传感器类 */
DHT                 dht(GPIO_DHT11, DHT11);
/* OLED SSD1306 */
SSD1306Wire         display(0x3C, GPIO_OLED_SDA, GPIO_OLED_SCL);
/* ui相关封装类 */
OLEDDisplayUi       ui(&display);
/* WiFi连接事件句柄 */
WiFiEventHandler wifiConnectHandler;
/* WiFi断开事件句柄 */
WiFiEventHandler wifiDisconnectHandler;

/* LED 相关设置 */
LED_S       LED = {true, 0, 0, 0, 0};
/* 日期时间相关设置 */
DATETIME_S  DATETIME = {true, 2022, 8, 12, 8, 0, 0}, DATETIME2;
/* 天气信息相关设置 */
WEATHER_S   WEATHER = {true, 0.0, 0.0, 0, 0, 0, 0};
/* 同步相关设置 */
SYNC_S      SYNC = {0, SYNC_TIME_INTERVAL, SYNC_WEATHER_INTERVAL, 0, false, false, MQTT_PORT, String(MQTT_HOST)};
/* WIFI相关设置 */
WIFI_S      WIFI = {false, 10, String(WIFI_SSID), String(WIFI_PASS)};
/* 屏幕相关设置 */
SCREEN_S    SCREEN = {false, false, 0, 0, 0, 0};
/* 菜单相关 */
MENU_S      MENU = {false, 0, 0, 0};

/* 帧集合 */
FrameCallback frames[] = {
    uiDrawFrameGIF,
    uiDrawFrameDatetime,
    uiDrawFrameHygrothermograph,
    uiDrawFrameWeather,
    uiDrawFrameMenu,
    uiDrawFrameSubmenu,
    uiDrawFrameItem
};
/* 覆盖层集合 */
OverlayCallback overlays[] = { uiOverlay };

#pragma endregion
#pragma region CONSTANTS

/* 帧总数 */
const uint8_t framesCount = 7;
/* 覆盖层总数 */
const uint8_t overlaysCount = 1;
/* 主菜单数 */
const uint8_t   menu_length             = 6;
/* 主菜单选项，子菜单标题 */
const char      menu_title[6][8]        = {"Screen", "LED", "Clock", "Weather", "Sync", "System"};
/* 子菜单数 */
const uint8_t   submenu_length[6]       = {3, 4, 2, 1, 3, 1};
/* 子菜单选项 */
const char      submenu_title[7][4][24] = {
    {"Sensor", "Scroll Interval", "Lock Interval", ""},
    {"Sensor", "Delay OFF time", "Button ON time", "Remote ON time"},
    {"Type", "Adjust date and time", "", ""},
    {"Units", "", "", ""},
    {"Sync Data Interval", "Sync Time Interval", "Sync Weather Interval", ""},
    {"Upgrade", "", "", ""}
};
/* 开关选项 */
const char      items_1[2][24]          = {"ON", "OFF"};
/* 温度单位选项 */
const char      items_2[2][24]          = {"Celsius", "Fahrenheit"};
/* 小时制选项 */
const char      items_3[2][24]          = {"24-hour clock", "12-hour clock"};

#pragma endregion
#pragma region UI

/**
 * @brief 绘制UI覆盖层
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 */
void uiOverlay(OLEDDisplay *display, OLEDDisplayUiState *state) {
    /* 第 2 帧到第 4 帧为主页面，在主页面时显示状态栏 */
    if (SCREEN.currentFrame > 0 && SCREEN.currentFrame < 4) {
        /* 左上角显示WIFI状态 */
        display->drawXbm(0, 0, ICON_WIDTH, ICON_HEIGHT, icon_wifi[WIFI.isConnected]);
        /* 左上角显示同步状态 */
        if (SYNC.failedCount > MAX_FAIL_COUNTS) {
            /* 超过最大失败次数，说明同步出现问题 */
            display->drawXbm(16, 0, ICON_WIDTH, ICON_HEIGHT, icon_sync[1]);
        } else if (SYNC.failedCount > 0) {
            display->drawXbm(16, 0, ICON_WIDTH, ICON_HEIGHT, icon_sync[0]);
        }
        /* 右上角显示LED状态 */
        if (digitalRead(GPIO_LED) == HIGH) {
            display->drawXbm(112, 0, ICON_WIDTH, ICON_HEIGHT, icon_led);
        }
    }
}

/**
 * @brief 绘制小电视加载动画
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameGIF(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    static uint8_t i = 0;
    /* 使用一个变量缓冲一下，不然动画太快 */
    i = (i == 13) ? 0 : i + 1;
    display->drawXbm(36 + x, 3 + y, GIF_WIDTH, GIF_HEIGHT, gif_loading[(i>>1)]);
}

/**
 * @brief 绘制日期和时钟页面
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameDatetime(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    char buffer[8];
    /* 设置文本对齐方式 */
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    /* 设置字体 */
    display->setFont(ArialMT_Plain_16);
    /* 绘制文本 */
    display->drawStringf(19 + x, 16 + y, buffer,  "%4d", DATETIME.year);
    display->drawStringf(64 + x, 16 + y, buffer, "%02d", DATETIME.month);
    display->drawStringf(89 + x, 16 + y, buffer, "%02d", DATETIME.day);
    display->drawString(58 + x, 14 + y, "-");
    display->drawString(83 + x, 14 + y, "-");
    /* 设置字体 */
    display->setFont(ArialMT_Plain_24);
    /* 绘制文本 */
    display->drawStringf(49 + x, 32 + y, buffer, "%02d", DATETIME.minute);
    display->drawStringf(84 + x, 32 + y, buffer, "%02d", DATETIME.second);
    display->drawString(41 + x, 30 + y, ":");
    display->drawString(77 + x, 30 + y, ":");
    if (DATETIME.is24) {
        display->drawStringf(14 + x, 32 + y, buffer, "%02d", DATETIME.hour);
    } else {
        display->drawStringf(14 + x, 32 + y, buffer, "%02d", DATETIME.hour >= 13 ? DATETIME.hour - 12 : DATETIME.hour);
        /* 设置文本对齐方式 */
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        /* 设置字体 */
        display->setFont(ArialMT_Plain_10);
        /* 绘制文本 */
        display->drawString(14 + x, 32 + y, DATETIME.hour >= 13 ? "PM" : "AM");
    }
}

/**
 * @brief 绘制温湿度计页面
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameHygrothermograph(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    char buffer[8];
    /* 设置文本对齐方式 */
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    /* 设置字体 */
    display->setFont(ArialMT_Plain_16);
    /* 绘制图标 */
    display->drawXbm(24 + x, 18 + y, SIGN_WIDTH, SIGN_HEIGHT, icon_pic_tem);
    display->drawXbm(80 + x, 18 + y, SIGN_WIDTH, SIGN_HEIGHT, icon_pic_hum);
    /* 绘制文本 */
    display->drawStringf(48 + x, 46 + y, buffer, "%4.1f", dht11Temp);
    display->drawStringf(96 + x, 46 + y, buffer, "%2.0f", dht11Humi);
    /* 绘制单位 */
    display->drawXbm(48 + x, 48 + y, ICON_WIDTH, ICON_HEIGHT, icon_degree[WEATHER.unit]);
    display->drawXbm(96 + x, 48 + y, ICON_WIDTH, ICON_HEIGHT, icon_percent);
}

/**
 * @brief 绘制天气页面
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameWeather(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    char buffer[32];
    /* 设置文本对齐方式 */
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    /* 设置字体 */
    display->setFont(ArialMT_Plain_10);
    /* 绘制天气图标，未完成 */
    //display->drawXbm(0 + x, 16 + y, ICON_WIDTH, ICON_HEIGHT, );
    /* 绘制文本 */
    display->drawStringf(64 + x, 16 + y, buffer, "%s: %s", WEATHER.city.c_str(), WEATHER.text.c_str());
    display->drawStringf(64 + x, 32 + y, buffer, "Temp: %3.1f°, Humi: %2d%", WEATHER.temp, WEATHER.humi);
    display->drawStringf(64 + x, 42 + y, buffer, "Wind: %s%2d, Precip:%3.1fml", WEATHER.wind.c_str(), WEATHER.windScale, WEATHER.precip);
    display->drawStringf(64 + x, 52 + y, buffer, "Pressure: %4dhPa", WEATHER.pressure);
}

/**
 * @brief 绘制主菜单页面
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameMenu(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    /* 设置文本对齐方式 */
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    /* 设置字体 */
    display->setFont(ArialMT_Plain_10);
    /* 绘制标题 */
    display->drawString(64 + x, 51 + y, menu_title[MENU.current[0]]);
    /* 绘制左右箭头 */
    display->drawXbm( 12 + x, 24 + y, ICON_WIDTH, ICON_HEIGHT, icon_left);
    display->drawXbm(100 + x, 24 + y, ICON_WIDTH, ICON_HEIGHT, icon_right);
    /* 绘制图标 */
    display->drawXbm( 40 + x,  1 + y, MENU_WIDTH, MENU_HEIGHT, icon_menu[MENU.current[0]]);
}

/**
 * @brief 绘制子菜单页面
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameSubmenu(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    uint8_t i, j = MENU.current[1] / 3, k = 0, length = submenu_length[MENU.current[0]];
    /* 设置文本对齐方式 */
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    /* 设置字体 */
    display->setFont(ArialMT_Plain_10);
    /* 绘制子菜单标题 */
    display->drawString(64 + x, 0 + y, menu_title[MENU.current[0]]);
    /* 设置文本对齐方式 */
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    /* 循环绘制子菜单项目列表 */
    for (i = 14; i < 58 && (3 * j + k) < length; i += 18, k++) {
        /* 判断子菜单项目是否选中 */
        if (MENU.current[1] == 3 * j + k) {
            /* 先填充矩形 */
            display->fillRect(0 + x, i + y, 128, 14);
            /* 设置颜色为暗色 */
            display->setColor(BLACK);
            /* 绘制出阴文形式的文本 */
            display->drawString(0 + x, i + y, submenu_title[MENU.current[0]][j * 3 + k]);
            /* 调整颜色为亮色，避免之后的绘制出现错误 */
            display->setColor(WHITE);
        } else {
            /* 没有选中，则正常绘制文本 */
            display->drawString(0 + x, i + y, submenu_title[MENU.current[0]][j * 3 + k]);
        }
    }
}

/**
 * @brief 绘制设置项
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameItem(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    /* 这里涉及到操作逻辑，会比较长 */
    if (MENU.current[0] == 0) {
        if (MENU.current[1] == 0) {
            uiDrawItemCheck(display, x, y, submenu_title[0][0], items_1, 2, MENU.current[2]);
        } else if (MENU.current[1] < 3) {
            uiDrawItemNumber(display, x, y, submenu_title[0][MENU.current[1]], MENU.reserved, "Seconds");
        }
    } else if (MENU.current[0] == 1) {
        if (MENU.current[1] == 0) {
            uiDrawItemCheck(display, x, y, submenu_title[1][0], items_1, 2, MENU.current[2]);
        } else if (MENU.current[1] < 4) {
            uiDrawItemNumber(display, x, y, submenu_title[1][MENU.current[1]], MENU.reserved, "Seconds");
        }
    } else if (MENU.current[0] == 2) {
        if (MENU.current[1] == 0) {
            uiDrawItemCheck(display, x, y, submenu_title[2][0], items_3, 2, MENU.current[2]);
        } else if (MENU.current[1] == 1) {
            uiDrawItemDatetime(display, x, y, submenu_title[2][1], MENU.current[2], (bool)MENU.reserved);
        }
    } else if (MENU.current[0] == 3) {
        if (MENU.current[1] == 0) {
            uiDrawItemCheck(display, x, y, submenu_title[3][0], items_2, 2, MENU.current[2]);
        }
    } else if (MENU.current[0] == 4) {
        if (MENU.current[1] < 3) {
            uiDrawItemNumber(display, x, y, submenu_title[5][MENU.current[1]], MENU.reserved, MENU.current[1] == 0 ? "Seconds" : "* 2 Minutes");
        }
    } else if (MENU.current[0] == 5) {
        if (MENU.current[1] == 0) {
            uiDrawItemUpgrade(display, x, y, MENU.reserved);
        }
    }
}

/**
 * @brief 绘制单选设置项
 * @param {OLEDDisplay} *display
 * @param {int16_t} x
 * @param {int16_t} y
 * @param {char} *title 标题
 * @param {char} value[][32] 各个选项
 * @param {uint8_t} value_len 选项个数
 * @param {uint8_t} current 当前选中选项
 */
void uiDrawItemCheck(OLEDDisplay *display, int16_t x, int16_t y, const char *title, const char value[][24], uint8_t value_len, uint8_t current) {
    uint8_t i, j = current / 3, k = 0;
    /* 设置文本对齐方式 */
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    /* 设置字体 */
    display->setFont(ArialMT_Plain_10);
    /* 绘制标题 */
    display->drawString(64 + x, 0 + y, title);
    /* 循环绘制选项列表 */
    for (i = 14; i < 58 && (3 * j + k) < value_len; i += 18, k++) {
        /* 判断选项是否选中 */
        if (current == j * 3 + k) {
            /* 先填充矩形 */
            display->fillRect(0 + x, i + y, 128, 14);
            /* 设置颜色为暗色 */
            display->setColor(BLACK);
            /* 绘制出阴文形式的文本 */
            display->drawString(64 + x, i + y, value[j * 3 + k]);
            /* 调整颜色为亮色，避免之后的绘制出现错误 */
            display->setColor(WHITE);
        } else {
            /* 没有选中，则正常绘制文本 */
            display->drawString(64 + x, i + y, value[j * 3 + k]);
        }
    }
}

/**
 * @brief 绘制数值设置项
 * @param {OLEDDisplay} *display
 * @param {int16_t} x
 * @param {int16_t} y
 * @param {char} *title 标题
 * @param {uint8_t} value 当前数值
 * @param {char} *units 单位
 */
void uiDrawItemNumber(OLEDDisplay *display, int16_t x, int16_t y, const char *title, uint16_t value, const char *units) {
    /* 设置文本对齐方式 */
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    /* 设置字体 */
    display->setFont(ArialMT_Plain_10);
    /* 绘制标题 */
    display->drawString(64 + x, 0 + y, title);
    /* 绘制加减号 */
    display->drawXbm( 12 + x, 24 + y, ICON_WIDTH, ICON_HEIGHT, icon_minus);
    display->drawXbm(100 + x, 24 + y, ICON_WIDTH, ICON_HEIGHT, icon_plus);
    /* 绘制单位 */
    display->drawString(64 + x, 48 + y, units);
    /* 设置字体 */
    display->setFont(ArialMT_Plain_24);
    /* 绘制数值 */
    display->drawString(64 + x, 20 + y, String(value));
}

/**
 * @brief 绘制日期时间设置项
 * @param {OLEDDisplay} *display
 * @param {int16_t} x
 * @param {int16_t} y
 * @param {char} *title
 * @param {uint8_t} current
 * @param {bool} state
 */
void uiDrawItemDatetime(OLEDDisplay *display, int16_t x, int16_t y, const char *title, uint8_t current, bool state) {
    char buffer[8];
    /* 设置文本对齐方式 */
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    /* 设置字体 */
    display->setFont(ArialMT_Plain_10);
    /* 绘制标题 */
    display->drawString(64 + x, 0 + y, title);
    /* 设置文本对齐方式 */
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    /* 判断是否是设置状态 */
    if (!state) {
        /* 先绘制一层底层 */
        display->setFont(ArialMT_Plain_16);
        display->drawStringf(19 + x, 16 + y, buffer,  "%4d", DATETIME2.year);
        display->drawStringf(64 + x, 16 + y, buffer, "%02d", DATETIME2.month);
        display->drawStringf(89 + x, 16 + y, buffer, "%02d", DATETIME2.day);
        display->drawString(58 + x, 14 + y, "-");
        display->drawString(83 + x, 14 + y, "-");
        display->setFont(ArialMT_Plain_24);
        display->drawStringf(49 + x, 32 + y, buffer, "%02d", DATETIME2.minute);
        display->drawStringf(84 + x, 32 + y, buffer, "%02d", DATETIME2.second);
        display->drawString(41 + x, 30 + y, ":");
        display->drawString(77 + x, 30 + y, ":");
        if (DATETIME.is24) {
            display->drawStringf(14 + x, 32 + y, buffer, "%02d", DATETIME.hour);
        } else {
            display->drawStringf(14 + x, 32 + y, buffer, "%02d", DATETIME.hour >= 13 ? DATETIME.hour - 12 : DATETIME.hour);
            display->setTextAlignment(TEXT_ALIGN_RIGHT);
            display->setFont(ArialMT_Plain_10);
            display->drawString(14 + x, 32 + y, DATETIME.hour >= 13 ? "PM" : "AM");
        }
    }
    /* 根据当前选中项绘制覆盖层 */
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_16);
    if (current == 0) {
        display->fillRect(19 + x, 18 + y, 36, 14);
        display->setColor(BLACK);
        display->drawStringf(19 + x, 16 + y, buffer,  "%4d", DATETIME2.year);
    } else if (current == 1) {
        display->fillRect(64 + x, 18 + y, 18, 14);
        display->setColor(BLACK);
        display->drawStringf(64 + x, 16 + y, buffer, "%02d", DATETIME2.month);
    } else if (current == 2) {
        display->fillRect(89 + x, 18 + y, 18, 14);
        display->setColor(BLACK);
        display->drawStringf(89 + x, 16 + y, buffer, "%02d", DATETIME2.day);
    }
    display->setFont(ArialMT_Plain_24);
    if (current == 4) {
        display->fillRect(49 + x, 36 + y, 26, 19);
        display->setColor(BLACK);
        display->drawStringf(49 + x, 32 + y, buffer, "%02d", DATETIME2.minute);
    } else if (current == 5) {
        display->fillRect(84 + x, 36 + y, 26, 19);
        display->setColor(BLACK);
        display->drawStringf(84 + x, 32 + y, buffer, "%02d", DATETIME2.second);
    } else if (current == 3) {
        display->fillRect(14 + x, 36 + y, 26, 19);
        display->setColor(BLACK);
        if (DATETIME.is24) {
            display->drawStringf(14 + x, 32 + y, buffer, "%02d", DATETIME.hour);
        } else {
            display->drawStringf(14 + x, 32 + y, buffer, "%02d", DATETIME.hour >= 13 ? DATETIME.hour - 12 : DATETIME.hour);
            display->setColor(WHITE);
            display->setTextAlignment(TEXT_ALIGN_RIGHT);
            display->setFont(ArialMT_Plain_10);
            display->drawString(14 + x, 32 + y, DATETIME.hour >= 13 ? "PM" : "AM");
        }
    }
    display->setColor(WHITE);
}

/**
 * @brief 绘制升级页面
 * @param {OLEDDisplay} *display
 * @param {int16_t} x
 * @param {int16_t} y
 * @param {uint16_t} state 升级状态
 */
void uiDrawItemUpgrade(OLEDDisplay *display, int16_t x, int16_t y, uint16_t state) {
    /* 使用菜单预留位判断需要绘制的内容 */
    if (MENU.reserved == 0) {
        /* 设置文本对齐方式 */
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        /* 设置字体 */
        display->setFont(ArialMT_Plain_10);
        /* 绘制标题 */
        display->drawString(64 + x, 51 + y, "Upgrading...");
        /* 绘制图标 */
        display->drawXbm(40 + x,  1 + y, MENU_WIDTH, MENU_HEIGHT, icon_upgrade);
    } else {
        /* 绘制边框 */
        display->drawRect(0 + x, 16 + y, 128, 32);
        /* 设置文本对齐方式 */
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        /* 设置字体 */
        display->setFont(ArialMT_Plain_10);
        /* 绘制提示信息 */
        if (MENU.reserved == 1) {
            display->drawString(64 + x, 32 + y, "Successed, restarting...");
        } else if (MENU.reserved == 2) {
            display->drawString(64 + x, 32 + y, "No latest firmware.");
        } else {
            display->drawStringMaxWidth(0 + x, 16 + y, 128, "Fail. " + ESPhttpUpdate.getLastErrorString());
        }
    }
}

/**
 * @brief 初始化屏幕和UI
 */
void initUI() {
    /* 设置目标帧率 */
    ui.setTargetFPS(30);
    /* 禁用指示器 */
    ui.disableAllIndicators();
    /* 禁用自动滚动 */
    ui.disableAutoTransition();
    /* 设置帧 */
    ui.setFrames(frames, framesCount);
    /* 设置覆盖层 */
    ui.setOverlays(overlays, overlaysCount);
    /* 设置显示第一帧(SCREEN.currentFrame = 0) */
    ui.switchToFrame(SCREEN.currentFrame);
    /* 旋转180度显示 */
    //display.flipScreenVertically();
    /* 初始化 */
    ui.init();
    LOG.print("[info] UI: Init done.\n");
}

#pragma endregion
#pragma region WIFI

/**
 * @brief WiFi连接成功回调
 * @param {WiFiEventStationModeGotIP&} event
 */
void wifiOnConnect(const WiFiEventStationModeGotIP& event) {
    (void) event;
    LOG.print("[info] WiFi: Connected to WiFi\n");
    WIFI.isConnected = true;
    requestTimeSend();
    requestWeatherSend();
    tickerReconnect.detach();
    tickerReconnect.once(RETRY_INTERVAL, mqttConnect);
}

/**
 * @brief WiFi断开连接回调
 * @param {WiFiEventStationModeDisconnected&} event
 */
void wifiOnDisconnect(const WiFiEventStationModeDisconnected& event) {
    (void) event;
    LOG.print("[warn] WiFi: Disonnected from WiFi\n");
    WIFI.isConnected = false;
    wifiConnect();
}

/**
 * @brief 连接WiFi
 */
void wifiConnect() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI.wifiSSID, WIFI.wifiPASS);
    LOG.print("[info] WiFi: Connecting wifi...\n");
    LOG.printf("SSID: %s, PASS: %s\n", WIFI.wifiSSID.c_str(), WIFI.wifiPASS.c_str());
}

/**
 * @brief 初始化WiFi
 */
void initWiFi() {
    wifiConnectHandler = WiFi.onStationModeGotIP(wifiOnConnect);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(wifiOnDisconnect);
    wifiConnect();
    LOG.print("[info] WiFi: Init done.\n");
}

#pragma endregion
#pragma region MQTT

/**
 * @brief MQTT连接成功回调
 * @param {bool} sessionPresent 持久性会话
 */
void mqttOnConnect(bool sessionPresent) {
    LOG.printf("[info] MQTT: Connected to Broker: %s:%d\n", SYNC.mqttHost.c_str(), SYNC.mqttPort);
    tickerReconnect.detach();
    SYNC.mqttIsConnected = true;
    /* 系统配置 */
    MQTT.subscribe(MQTT_SUB_CONFIG, 0);
    /* 固件升级 */
    MQTT.subscribe(MQTT_SUB_UPGRADE, 2);
    /* 用户设置 */
    MQTT.subscribe(MQTT_SUB_SETTINGS, 0);
    /* 远程控制 */
    MQTT.subscribe(MQTT_SUB_CONTROL, 2);
    /* 服务器在线状态 */
    MQTT.subscribe(MQTT_SUB_SERVER, 0);
    /* 发布设备在线信息 */
    MQTT.publish(MQTT_SUB_STATUS, 0, true, "{\"online\": true}");
}

/**
 * @brief MQTT断开连接回调
 * @param {AsyncMqttClientDisconnectReason} reason 连接断开代码
 */
void mqttOnDisconnect(AsyncMqttClientDisconnectReason reason) {
    LOG.print("[warn] MQTT: Disconnection from Broker.\n");
    SYNC.mqttIsConnected = false;
    if (WIFI.isConnected) {
        /* 若WiFi已连接，则尝试重连 */
        tickerReconnect.detach();
        tickerReconnect.once(RETRY_INTERVAL, mqttConnect);
    }
}

/**
 * @brief MQTT接受消息回调
 * @param {char*} topic 主题
 * @param {char*} payload 消息内容
 * @param {AsyncMqttClientMessageProperties&} properties 消息属性(qos, dup, retain)
 * @param {size_t&} len 消息长度
 * @param {size_t&} index
 * @param {size_t&} total
 */
void mqttOnReceive(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, const size_t& len, const size_t& index, const size_t& total) {
    /* 截断消息，去掉后面的乱码 */
    String msg = String(payload).substring(0, len);
    LOG.printf("[info] MQTT: Massage Receive (topic: %s, qos: %d, retain: %d): \n%s\n", topic, properties.qos, (int32_t)properties.retain, msg.c_str());
    /* 解析JSON格式 */
    jsonDoc.clear();
    DeserializationError err = deserializeJson(jsonDoc, msg);
    if (err)return;
    /* 判断订阅主题 */
    if (strcmp(topic, MQTT_SUB_CONFIG) == 0) {
        if (apiTimeUrl != jsonDoc["apiTimeUrl"] && jsonDoc["apiTimeUrl"] != "") {
            apiTimeUrl = String(jsonDoc["apiTimeUrl"]);
        }
        if (apiWeatherUrl != jsonDoc["apiWeatherUrl"] && jsonDoc["apiWeatherUrl"] != "") {
            apiWeatherUrl = String(jsonDoc["apiWeatherUrl"]);
        }
        if (apiWeatherKey != jsonDoc["apiWeatherKey"] && jsonDoc["apiWeatherKey"] != "") {
            apiWeatherKey = String(jsonDoc["apiWeatherKey"]);
        }
        LOG.print("[info] MQTT: Receive configuration.\n");
    } else if (strcmp(topic, MQTT_SUB_UPGRADE) == 0) {
        bool forced         = jsonDoc["forced"];
        upgradeUrl          = String(jsonDoc["upgradeUrl"]);
        upgradeVersion      = jsonDoc["version"].as<uint32_t>();
        if (forced && upgradeVersion > SKETCH_VERSION) {
            autoUpgrade = true;
            configSave();
            ESP.restart();
        }
        LOG.print("[info] MQTT: Receive upgrade.\n");
    } else if (strcmp(topic, MQTT_SUB_SETTINGS) == 0) {
        DATETIME.is24       = jsonDoc["datetime"]["is24"];
        LED.sensorOn        = jsonDoc["led"]["sensorOn"];
        LED.delayOffTime    = jsonDoc["led"]["delayOffTime"];
        LED.buttonOnTime    = jsonDoc["led"]["buttonOnTime"];
        LED.remoteOnTime    = jsonDoc["led"]["remoteOnTime"];
        SCREEN.sensorOn     = jsonDoc["SCREEN"]["sensorOn"];
        SCREEN.lockInterval     = jsonDoc["SCREEN"]["lockInterval"];
        SCREEN.scrollInterval   = jsonDoc["SCREEN"]["scrollInterval"];
        if (SYNC.dataSyncInterval != jsonDoc["sync"]["dataSyncInterval"] && jsonDoc["sync"]["dataSyncInterval"] >= MIN_SYNC_INTERVAL) {
            SYNC.dataSyncInterval = jsonDoc["sync"]["dataSyncInterval"];
            tickerSyncData.detach();
            tickerSyncData.attach(SYNC.dataSyncInterval, mqttDataSend);
        }
        if (SYNC.clockSyncInterval != jsonDoc["sync"]["clockSyncInterval"] && 1 <= jsonDoc["sync"]["clockSyncInterval"] && jsonDoc["sync"]["clockSyncInterval"] <= 999) {
            SYNC.clockSyncInterval  = jsonDoc["sync"]["clockSyncInterval"];
            requestTimeSend();
            tickerSyncClock.detach();
            tickerSyncClock.attach(SYNC.clockSyncInterval * 120, requestTimeSend);
        }
        if (SYNC.weatherSyncInterval != jsonDoc["sync"]["weatherSyncInterval"] && 1 <= jsonDoc["sync"]["weatherSyncInterval"] && jsonDoc["sync"]["clockSyncInterval"] <= 999) {
            SYNC.weatherSyncInterval = jsonDoc["sync"]["weatherSyncInterval"];
            tickerSyncWeather.detach();
            tickerSyncWeather.attach(SYNC.weatherSyncInterval * 120, requestWeatherSend);
        }
        if (WEATHER.unit != jsonDoc["weather"]["unit"] || WEATHER.city != jsonDoc["weather"]["city"] || WEATHER.cityID != jsonDoc["weather"]["cityID"]) {
            WEATHER.unit    = jsonDoc["weather"]["unit"];
            WEATHER.city    = String(jsonDoc["weather"]["city"]);
            WEATHER.cityID  = String(jsonDoc["weather"]["cityID"]);
            requestWeatherSend();
        }
        LOG.print("[info] MQTT: Receive settings.\n");
    } else if (strcmp(topic, MQTT_SUB_CONTROL) == 0) {
        bool state          = jsonDoc["LED"];
        String BTN          = String(jsonDoc["BTN"]);
        if (state) {
            LED.state = 3;
            ledSetState(true, LED.remoteOnTime);
        } else {
            ledSetState(false, LED.remoteOnTime);
        }
        if (BTN == "LS") {
            lButtonShortClick();
        } else if (BTN == "LL") {
            lButtonLongClick();
        } else if (BTN == "RS") {
            rButtonShortClick();
        } else if (BTN == "RL") {
            rButtonLongClick();
        }
        LOG.print("[info] MQTT: Receive control command.\n");
    } else if (strcmp(topic, MQTT_SUB_SERVER) == 0) {
        SYNC.serverState    = jsonDoc["online"];
        if (SYNC.serverState) {
            tickerSyncData.detach();
            tickerSyncData.attach(SYNC.dataSyncInterval, mqttDataSend);
        } else {
            tickerSyncData.detach();
        }
        LOG.print("[info] MQTT: Receive server's state.\n");
    }
}

/**
 * @brief MQTT发布消息回调
 * @param {uint16_t&} packetId 消息标识
 */
void mqttOnPublish(const uint16_t& packetId) {
    LOG.print("[info] MQTT: Publish success.\n");
    SYNC.failedCount = 0;
}

/**
 * @brief 尝试连接 MQTT Broker
 */
void mqttConnect() {
    LOG.print("[info] MQTT: Connecting to Broker...\n");
    MQTT.connect();
}

/**
 * @brief 通过MQTT发送温湿度数据
 */
void mqttDataSend() {
    if (!SYNC.mqttIsConnected || !SYNC.serverState)return;
    char buffer[32];
    sprintf(buffer, "{\n  \"temp\":%3.1f,\n  \"humi\":%3.0f\n}", dht11Temp, dht11Humi);
    MQTT.publish(MQTT_SUB_DATA, 0, false, buffer);
}

/**
 * @brief 通过MQTT发送用户设置
 */
void mqttSettingsSend() {
    if (!SYNC.mqttIsConnected)return;
    char buffer[384];
    jsonDoc.clear();
    jsonDoc["datetime"]["is24"]         = DATETIME.is24;
    jsonDoc["led"]["sensorOn"]          = LED.sensorOn;
    jsonDoc["led"]["delayOffTime"]      = LED.delayOffTime;
    jsonDoc["led"]["buttonOnTime"]      = LED.buttonOnTime;
    jsonDoc["led"]["remoteOnTime"]      = LED.remoteOnTime;
    jsonDoc["screen"]["sensorOn"]       = SCREEN.sensorOn;
    jsonDoc["screen"]["lockInterval"]       = SCREEN.lockInterval;
    jsonDoc["screen"]["scrollInterval"]     = SCREEN.scrollInterval;
    jsonDoc["sync"]["dataSyncInterval"]     = SYNC.dataSyncInterval;
    jsonDoc["sync"]["clockSyncInterval"]    = SYNC.clockSyncInterval;
    jsonDoc["sync"]["weatherSyncInterval"]  = SYNC.weatherSyncInterval;
    jsonDoc["weather"]["unit"]          = WEATHER.unit;
    jsonDoc["weather"]["city"]          = WEATHER.city;
    jsonDoc["weather"]["cityID"]        = WEATHER.cityID;
    serializeJson(jsonDoc, buffer, 384);
    MQTT.publish(MQTT_SUB_SETTINGS, 0, true, buffer);
}

/**
 * @brief 初始化MQTT
 */
void initMQTT() {
    MQTT.onConnect(mqttOnConnect);
    MQTT.onDisconnect(mqttOnDisconnect);
    MQTT.onMessage(mqttOnReceive);
    MQTT.onPublish(mqttOnPublish);
    MQTT.setWill(MQTT_SUB_STATUS, 0, true, "{\"online\": false}");
    MQTT.setServer(SYNC.mqttHost.c_str(), SYNC.mqttPort);
    LOG.print("[info] MQTT: Init done.\n");
}

#pragma endregion
#pragma region HTTP

/**
 * @brief 发送HTTP请求网络时间
 */
void requestTimeSend() {
    if (WIFI.isConnected) {
        if (requestTime.readyState() == readyStateUnsent || requestTime.readyState() == readyStateDone) {
            bool ret = requestTime.open("GET", apiTimeUrl.c_str());
            if (ret) {
                requestTime.send();
            } else {
                LOG.print("[warn] RequestTime: send request failed\n");
            }
        } else {
            LOG.print("[warn] RequestTime: can't send request\n");
        }
    }
}

/**
 * @brief 接受HTTP响应回调，并解析时间信息
 * @param {void*} optParm
 * @param {AsyncHTTPRequest*} request
 * @param {int} readyState
 */
void requestTimeOnReceive(void* optParm, AsyncHTTPRequest* request, int readyState) {
    String tDatetime;
    if (readyState == readyStateDone) {
        DeserializationError error = deserializeJson(jsonDoc, request->responseText());
        tickerSyncClock.detach();
        if (!error) {
            tDatetime = String(jsonDoc["datetime"]);
            DATETIME.year   = tDatetime.substring( 0, 4).toInt();
            DATETIME.month  = tDatetime.substring( 5, 7).toInt();
            DATETIME.day    = tDatetime.substring( 8,10).toInt();
            DATETIME.hour   = tDatetime.substring(11,13).toInt();
            DATETIME.minute = tDatetime.substring(14,16).toInt();
            DATETIME.second = tDatetime.substring(17,19).toInt();
            LOG.printf("[info] Update internet time.\n");
            tickerSyncClock.once(SYNC.clockSyncInterval * 120, requestTimeSend);
        } else {
            tickerSyncClock.once(10, requestTimeSend);
        }
        request->setDebug(false);
    }
}

/**
 * @brief 发送HTTP请求天气信息
 */
void requestWeatherSend() {
    char buffer[192];
    if (WIFI.isConnected) {
        if (requestWeather.readyState() == readyStateUnsent || requestWeather.readyState() == readyStateDone) {
            sprintf(buffer, "%s?lang=en&unit=%c&location=%s&key=%s", apiWeatherUrl.c_str(), (WEATHER.unit) ? 'm' : 'i', WEATHER.cityID.c_str(), apiWeatherKey.c_str());
            bool ret = requestWeather.open("GET", buffer);
            if (ret) {
                requestWeather.send();
            } else {
                LOG.print("[warn] RequestWeather: send request failed\n");
            }
        } else {
            LOG.print("[warn] RequestWeather: can't send request\n");
        }
    }
}

/**
 * @brief 接受HTTP响应回调，并解析天气信息
 * @param {void*} optParm
 * @param {AsyncHTTPRequest*} request
 * @param {int} readyState
 * @retval 
 */
void requestWeatherOnReceive(void* optParm, AsyncHTTPRequest* request, int readyState) {
    if (readyState == readyStateDone) {
        DeserializationError error = deserializeJson(jsonDoc, request->responseText());
        tickerSyncWeather.detach();
        if (!error) {
            WEATHER.temp        = jsonDoc["now"]["temp"].as<float>();
            WEATHER.precip      = jsonDoc["now"]["precip"].as<float>();
            WEATHER.humi        = jsonDoc["now"]["humidity"].as<uint8_t>();
            WEATHER.windScale   = jsonDoc["now"]["windScale"].as<uint8_t>();
            WEATHER.icon        = jsonDoc["now"]["icon"].as<uint16_t>();
            WEATHER.pressure    = jsonDoc["now"]["pressure"].as<uint16_t>();
            WEATHER.text        = String(jsonDoc["now"]["text"]);
            WEATHER.wind        = String(jsonDoc["now"]["windDir"]);
            LOG.printf("[info] Update weather.\n");
            tickerSyncWeather.once(SYNC.weatherSyncInterval * 120, requestWeatherSend);
        } else {
            tickerSyncClock.once(10, requestWeatherSend);
        }
        request->setDebug(false);
    }
}

/**
 * @brief 初始化HTTP异步请求
 */
void initRequest() {
    requestTime.setDebug(false);
    requestTime.onReadyStateChange(requestTimeOnReceive);
    requestWeather.setDebug(false);
    requestWeather.onReadyStateChange(requestWeatherOnReceive);
}

#pragma endregion
#pragma region BUTTON

/**
 * @brief 左按钮中断处理函数
 */
ICACHE_RAM_ATTR void _leftButtonItr() {
    static bool press = false, lift = false;
    static uint32_t dt = 0;
    if (digitalRead(GPIO_LBUTTON) == LOW) {
        /* 低电平，按下按钮 */
        press = true, dt = millis();
    } else if (press) {
        /* 高电平，按钮弹起 */
        lift = true, dt = millis() - dt;
    }
    /* 匹配每一次按钮按下和弹起 */
    if (press & lift) {
        press = lift = false;
        /* 通过间隔时间判断短按还是长按 */
        if (dt > LONG_CLICK_TIME) {
            lButtonLongClick();
        } else {
            lButtonShortClick();
        }
    }
}

/**
 * @brief 右按钮中断处理函数
 */
ICACHE_RAM_ATTR void _rightButtonItr() {
    static bool press = false, lift = false;
    static uint32_t dt = 0;
    if (digitalRead(GPIO_RBUTTON) == LOW) {
        /* 低电平，按下按钮 */
        press = true, dt = millis();
    } else if (press) {
        /* 高电平，按钮弹起 */
        lift = true, dt = millis() - dt;
    }
    /* 匹配每一次按钮按下和弹起 */
    if (press & lift) {
        press = lift = false;
        /* 通过间隔时间判断短按还是长按 */
        if (dt > LONG_CLICK_TIME) {
            rButtonLongClick();
        } else {
            rButtonShortClick();
        }
    }
}

/**
 * @brief 左按钮短按处理函数
 */
void lButtonShortClick() {
    if (initFlag) {
        if (SCREEN.isLocked) {
            /* 息屏则唤醒 */
            screenWakeup();
        } else if (MENU.isMainUI) {
            /* 主菜单页面则滚动 */
            screenScroll(false);
        } else if (MENU.level == 0) {
            /* 切换主菜单图标 */
            MENU.current[0] = (MENU.current[0] == 0) ? menu_length - 1 : MENU.current[0] - 1;
        } else if (MENU.level == 1) {
            /* 切换子菜单选项 */
            MENU.current[1] = (MENU.current[1] == 0) ? submenu_length[MENU.current[0]] - 1 : MENU.current[1] - 1;
        } else if (MENU.level == 2) {
            /* 根据每个选项的不同，定义不同的操作 */
            if (MENU.current[0] == 0) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = (MENU.current[2] == 0) ? 1 : MENU.current[2] - 1;
                } else if (MENU.current[1] < 3) {
                    MENU.reserved = (MENU.reserved == 0) ? 0 : MENU.reserved - 1;
                }
            } else if (MENU.current[0] == 1) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = (MENU.current[2] == 0) ? 1 : MENU.current[2] - 1;
                } else if (MENU.current[1] < 4) {
                    MENU.reserved = (MENU.reserved == 0) ? 0 : MENU.reserved - 1;
                }
            } else if (MENU.current[0] == 2) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = (MENU.current[2] == 0) ? 1 : MENU.current[2] - 1;
                } else if (MENU.current[1] == 1) {
                    MENU.current[2] = (MENU.current[2] == 0) ? 5 : MENU.current[2] - 1;
                }
            } else if (MENU.current[0] == 3) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = (MENU.current[2] == 0) ? 1 : MENU.current[2] - 1;
                }
            } else if (MENU.current[0] == 4) {
                if (MENU.current[1] == 0) {
                    MENU.reserved = (MENU.reserved == 3) ? 3 : MENU.reserved - 1;
                } else if (MENU.current[1] < 3) {
                    MENU.reserved = (MENU.reserved == 1) ? 1 : MENU.reserved - 1;
                }
            }
        } else if (MENU.level == 3) {
            if (MENU.current[0] == 2 && MENU.current[1] == 1) {
                /* 更改日期时间 */
                if (MENU.current[2] == 0) {
                    DATETIME2.year = (DATETIME2.year == 1) ? 1 : DATETIME2.year - 1;
                } else if (MENU.current[2] == 1) {
                    DATETIME2.month = (DATETIME2.month == 1) ? 12 : DATETIME2.month - 1;
                } else if (MENU.current[2] == 2) {
                    DATETIME2.day = (DATETIME2.day == 1) ? calcDays() : DATETIME2.day - 1;
                } else if (MENU.current[2] == 3) {
                    DATETIME2.hour = (DATETIME2.hour == 0) ? 23 : DATETIME2.hour - 1;
                } else if (MENU.current[2] == 4) {
                    DATETIME2.minute = (DATETIME2.minute == 0) ? 59 : DATETIME2.minute - 1;
                } else if (MENU.current[2] == 5) {
                    DATETIME2.second = (DATETIME2.second == 0) ? 59 : DATETIME2.second - 1;
                }
            }
        }
        screenResetTicker(true);
    }
}

/**
 * @brief 右按钮短按处理函数
 */
void rButtonShortClick() {
    if (initFlag) {
        if (SCREEN.isLocked) {
            /* 息屏则唤醒 */
            screenWakeup();
        } else if (MENU.isMainUI) {
            /* 主菜单页面则滚动 */
            screenScroll(true);
        } else if (MENU.level == 0) {
            /* 切换主菜单图标 */
            MENU.current[0] = (MENU.current[0] == menu_length - 1) ? 0 : MENU.current[0] + 1;
        } else if (MENU.level == 1) {
            /* 切换子菜单选项 */
            MENU.current[1] = (MENU.current[1] == submenu_length[MENU.current[0]] - 1) ? 0 : MENU.current[1] + 1;
        } else if (MENU.level == 2) {
            /* 根据每个选项的不同，定义不同的操作 */
            if (MENU.current[0] == 0) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = (MENU.current[2] == 1) ? 0 : MENU.current[2] + 1;
                } else if (MENU.current[1] < 3) {
                    MENU.reserved = (MENU.reserved == 999) ? 999 : MENU.reserved + 1;
                }
            } else if (MENU.current[0] == 1) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = (MENU.current[2] == 1) ? 0 : MENU.current[2] + 1;
                } else if (MENU.current[1] < 4) {
                    MENU.reserved = (MENU.reserved == 999) ? 999 : MENU.reserved + 1;
                }
            } else if (MENU.current[0] == 2) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = (MENU.current[2] == 1) ? 0 : MENU.current[2] + 1;
                } else if (MENU.current[1] == 1) {
                    MENU.current[2] = (MENU.current[2] == 5) ? 0 : MENU.current[2] + 1;
                }
            } else if (MENU.current[0] == 3) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = (MENU.current[2] == 1) ? 0 : MENU.current[2] + 1;
                }
            } else if (MENU.current[0] == 4) {
                if (MENU.current[1] == 0) {
                    MENU.reserved = (MENU.reserved == 999) ? 999 : MENU.reserved + 1;
                } else if (MENU.current[1] < 3) {
                    MENU.reserved = (MENU.reserved == 999) ? 999 : MENU.reserved + 1;
                }
            }
        } else if (MENU.level == 3) {
            if (MENU.current[0] == 2 && MENU.current[1] == 1) {
                /* 更改日期时间 */
                if (MENU.current[2] == 0) {
                    DATETIME2.year = (DATETIME2.year == 9999) ? 9999 : DATETIME2.year + 1;
                } else if (MENU.current[2] == 1) {
                    DATETIME2.month = (DATETIME2.month == 12) ? 1 : DATETIME2.month + 1;
                } else if (MENU.current[2] == 2) {
                    DATETIME2.day = (DATETIME2.day == calcDays()) ? 1 : DATETIME2.day + 1;
                } else if (MENU.current[2] == 3) {
                    DATETIME2.hour = (DATETIME2.hour == 23) ? 0 : DATETIME2.hour + 1;
                } else if (MENU.current[2] == 4) {
                    DATETIME2.minute = (DATETIME2.minute == 59) ? 0 : DATETIME2.minute + 1;
                } else if (MENU.current[2] == 5) {
                    DATETIME2.second = (DATETIME2.second == 59) ? 0 : DATETIME2.second + 1;
                }
            }
        }
        screenResetTicker(true);
    }
}

/**
 * @brief 左按钮长按处理函数
 */
void lButtonLongClick() {
    if (initFlag) {
        if (SCREEN.isLocked) {
            /* 息屏则唤醒 */
            screenWakeup();
            /* 切换LED */
            LED.state = ledGetState() ? 0 : 2;
            ledSetState(!ledGetState(), LED.buttonOnTime);
        } else if (MENU.isMainUI) {
            /* 切换LED */
            LED.state = ledGetState() ? 0 : 2;
            ledSetState(!ledGetState(), LED.buttonOnTime);
        } else if (MENU.level == 0) {
            /* 确认子菜单，进入子菜单 */
            MENU.level = 1;
            MENU.current[1] = 0;
            screenScrollToFrame(2, 5);
        } else if (MENU.level == 1) {
            /* 确认选项，进入选项设置 */
            if (MENU.current[0] == 0) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = !(SCREEN.sensorOn);
                } else if (MENU.current[1] == 1) {
                    MENU.reserved = SCREEN.scrollInterval;
                } else if (MENU.current[1] == 2) {
                    MENU.reserved = SCREEN.lockInterval;
                }
            } else if (MENU.current[0] == 1) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = !(LED.sensorOn);
                } else if (MENU.current[1] == 1) {
                    MENU.reserved = LED.delayOffTime;
                } else if (MENU.current[1] == 2) {
                    MENU.reserved = LED.buttonOnTime;
                } else if (MENU.current[1] == 3) {
                    MENU.reserved = LED.remoteOnTime;
                }
            } else if (MENU.current[0] == 2) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = !(DATETIME.is24);
                } else if (MENU.current[1] == 1) {
                    MENU.current[2] = 0;
                    MENU.reserved = 0;
                    DATETIME2 = DATETIME;
                }
            } else if (MENU.current[0] == 3) {
                if (MENU.current[1] == 0) {
                    MENU.current[2] = !(WEATHER.unit);
                }
            } else if (MENU.current[0] == 4) {
                if (MENU.current[1] == 0) {
                    MENU.reserved = SYNC.dataSyncInterval;
                } else if (MENU.current[1] == 1) {
                    MENU.reserved = SYNC.clockSyncInterval;
                } else if (MENU.current[1] == 2) {
                    MENU.reserved = SYNC.weatherSyncInterval;
                }
            } else if (MENU.current[0] == 5) {
                if (MENU.current[1] == 0) {
                    //MENU.reserved = systemUpgrade();
                    autoUpgrade = true;
                    configSave();
                    ESP.restart();
                }
            }
            if (MENU.current[0] != 5 || MENU.current[1] != 0) {
                MENU.level = 2;
            }
            screenScrollToFrame(1, 6);
        } else if (MENU.level == 2) {
            /* 确认选项，保存并回到子菜单 */
            if (MENU.current[0] == 0) {
                if (MENU.current[1] == 0) {
                    SCREEN.sensorOn = !(MENU.current[2]);
                } else if (MENU.current[1] == 1) {
                    SCREEN.scrollInterval = MENU.reserved;
                    screenResetTicker(false);
                } else if (MENU.current[1] == 2) {
                    SCREEN.lockInterval = MENU.reserved;
                    screenResetTicker(true);
                }
            } else if (MENU.current[0] == 1) {
                if (MENU.current[1] == 0) {
                    LED.sensorOn = !(MENU.current[2]);
                } else if (MENU.current[1] == 1) {
                    LED.delayOffTime = MENU.reserved;
                } else if (MENU.current[1] == 2) {
                    LED.buttonOnTime = MENU.reserved;
                } else if (MENU.current[1] == 3) {
                    LED.remoteOnTime = MENU.reserved;
                }
            } else if (MENU.current[0] == 2) {
                if (MENU.current[1] == 0) {
                    DATETIME.is24 = !(MENU.current[2]);
                } else if (MENU.current[1] == 1) {
                    MENU.level = 3;
                    MENU.reserved = 1;
                }
            } else if (MENU.current[0] == 3) {
                if (MENU.current[1] == 0) {
                    WEATHER.unit = !(MENU.current[2]);
                }
            } else if (MENU.current[0] == 4) {
                if (MENU.current[1] == 0) {
                    SYNC.dataSyncInterval = MENU.reserved;
                    tickerSyncData.detach();
                    tickerSyncData.attach(SYNC.dataSyncInterval, mqttDataSend);
                } else if (MENU.current[1] == 1) {
                    SYNC.clockSyncInterval = MENU.reserved;
                    tickerSyncClock.detach();
                    tickerSyncClock.attach(SYNC.clockSyncInterval, requestTimeSend);
                } else if (MENU.current[1] == 2) {
                    SYNC.weatherSyncInterval = MENU.reserved;
                    tickerSyncWeather.detach();
                    tickerSyncWeather.attach(SYNC.weatherSyncInterval, requestWeatherSend);
                }
            }
            if (MENU.current[0] != 2 || MENU.current[1] != 1) {
                MENU.level = 1;
                screenScrollToFrame(3, 5);
                mqttSettingsSend();
                settingsSave();
            }
        } else if (MENU.level == 3) {
            if (MENU.current[0] == 2 && MENU.current[1] == 1) {
                /* 确认修改日期 */
                DATETIME = DATETIME2;
                MENU.level = 2;
                MENU.reserved = 0;
            }
        }
        screenResetTicker(true);
    }
}

/**
 * @brief 右按钮长按处理函数
 */
void rButtonLongClick() {
    if (initFlag) {
        if (SCREEN.isLocked) {
            /* 息屏则唤醒 */
            screenWakeup();
        } else if (MENU.isMainUI) {
            /* 进入主菜单 */
            SCREEN.lastFrame = SCREEN.currentFrame;
            MENU.isMainUI = false;
            MENU.level = 0;
            MENU.current[0] = 0;
            screenScrollToFrame(2, 4);
        } else if (MENU.level == 0) {
            /* 退出主菜单 */
            MENU.isMainUI = true;
            MENU.current[0] = 0;
            screenScrollToFrame(4, SCREEN.lastFrame);
        } else if (MENU.level == 1) {
            /* 退出子菜单 */
            MENU.level = 0;
            MENU.current[0] = 0;
            screenScrollToFrame(4, 4);
        } else if (MENU.level == 2) {
            /* 退出选项设置，不保存 */
            MENU.level = 1;
            screenScrollToFrame(3, 5);
        } else if (MENU.level == 3) {
            if (MENU.current[0] == 2 && MENU.current[1] == 1) {
                /* 退出日期设置，不保存 */
                MENU.level = 2;
                MENU.reserved = 0;
            }
        }
        screenResetTicker(true);
    }
}

#pragma endregion
#pragma region SPIFFS

/**
 * @brief 加载Flash中的系统配置
 */
void configLoad() {
    if (flagFS) {
        if (SPIFFS.exists("/config.json")) {
            File fs = SPIFFS.open("/config.json", "r");
            if (fs) {
                DeserializationError err = deserializeJson(jsonDoc, fs.readString());
                if (err) {
                    LOG.print("[error] FS: load local config(failed).\n");
                    return;
                }
                String tDatetime = String(jsonDoc["datetime"]);
                DATETIME.year   = tDatetime.substring( 0, 4).toInt();
                DATETIME.month  = tDatetime.substring( 5, 7).toInt();
                DATETIME.day    = tDatetime.substring( 8,10).toInt();
                DATETIME.hour   = tDatetime.substring(11,13).toInt();
                DATETIME.minute = tDatetime.substring(14,16).toInt();
                DATETIME.second = tDatetime.substring(17,19).toInt();
                WIFI.wifiSSID   = String(jsonDoc["wifiSSID"]);
                WIFI.wifiPASS   = String(jsonDoc["wifiPASS"]);
                SYNC.mqttHost   = String(jsonDoc["mqttHost"]);
                SYNC.mqttPort   = jsonDoc["mqttPort"].as<uint16_t>();
                apiTimeUrl      = String(jsonDoc["apiTimeUrl"]);
                apiWeatherUrl   = String(jsonDoc["apiWeatherUrl"]);
                apiWeatherKey   = String(jsonDoc["apiWeatherKey"]);
                autoUpgrade     = jsonDoc["autoUpgrade"];
                upgradeVersion  = jsonDoc["upgradeVersion"].as<uint32_t>();
                upgradeUrl      = String(jsonDoc["upgradeUrl"]);
                fs.close();
                LOG.print("[info] FS: load local config(done).\n");
            } else {
                LOG.print("[error] FS: can't open config.json.\n");
            }
        } else {
            LOG.print("[warn] FS: config.json doesn't exist.\n");
            configSave();
            configLoad();
        }
    }
}

/**
 * @brief 保存系统配置到Flash
 */
void configSave() {
    if (flagFS) {
        File fs = SPIFFS.open("/config.json", "w");
        if (fs) {
            char buffer[1024];
            jsonDoc.clear();
            sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d", DATETIME.year, DATETIME.month, DATETIME.day, DATETIME.hour, DATETIME.minute, DATETIME.second);
            jsonDoc["datetime"]         = buffer;
            jsonDoc["wifiSSID"]         = WIFI.wifiSSID.c_str();
            jsonDoc["wifiPASS"]         = WIFI.wifiPASS.c_str();
            jsonDoc["mqttHost"]         = "www.eflystudio.cn";
            jsonDoc["mqttPort"]         = 1883;
            jsonDoc["apiTimeUrl"]       = apiTimeUrl.c_str();
            jsonDoc["apiWeatherUrl"]    = apiWeatherUrl.c_str();
            jsonDoc["apiWeatherKey"]    = apiWeatherKey.c_str();
            jsonDoc["autoUpgrade"]      = autoUpgrade;
            jsonDoc["upgradeVersion"]   = upgradeVersion;
            jsonDoc["upgradeUrl"]       = upgradeUrl.c_str();
            serializeJson(jsonDoc, buffer, 1024);
            fs.print(buffer);
            fs.close();
            LOG.print("[info] FS: save local config(done).\n");
        } else {
            LOG.print("[error] FS: can't open config.json.\n");
        }
    }
}

/**
 * @brief 加载Flash中的用户设置
 */
void settingsLoad() {
    if (flagFS) {
        if (SPIFFS.exists("/settings.json")) {
            File fs = SPIFFS.open("/settings.json", "r");
            if (fs) {
                DeserializationError err = deserializeJson(jsonDoc, fs.readString());
                if (err) {
                    LOG.print("[error] load local settings(failed).\n");
                    return;
                }
                DATETIME.is24           = jsonDoc["datetime"]["is24"];
                LED.sensorOn            = jsonDoc["led"]["sensorOn"];
                LED.delayOffTime        = jsonDoc["led"]["delayOffTime"];
                LED.buttonOnTime        = jsonDoc["led"]["buttonOnTime"];
                LED.remoteOnTime        = jsonDoc["led"]["remoteOnTime"];
                SCREEN.sensorOn         = jsonDoc["SCREEN"]["sensorOn"];
                SCREEN.lockInterval     = jsonDoc["SCREEN"]["lockInterval"];
                SCREEN.scrollInterval   = jsonDoc["SCREEN"]["scrollInterval"];
                SYNC.dataSyncInterval   = jsonDoc["sync"]["dataSyncInterval"];
                SYNC.clockSyncInterval  = jsonDoc["sync"]["clockSyncInterval"];
                SYNC.weatherSyncInterval = jsonDoc["sync"]["weatherSyncInterval"];
                WEATHER.unit            = jsonDoc["weather"]["unit"];
                WEATHER.city            = String(jsonDoc["weather"]["city"]);
                WEATHER.cityID          = String(jsonDoc["weather"]["cityID"]);
                fs.close();
                LOG.print("[info] FS: load local settings(done).\n");
            } else {
                LOG.print("[error] FS: can't open settings.json.\n");
            }
        } else {
            LOG.print("[warn] FS: settings.json doesn't exist.\n");
            settingsSave();
            settingsLoad();
        }
    }
}

/**
 * @brief 保存用户设置到Flash
 */
void settingsSave() {
    if (flagFS) {
        File fs = SPIFFS.open("/settings.json", "w");
        if (fs) {
            char buffer[1024];
            jsonDoc.clear();
            jsonDoc["datetime"]["is24"]             = DATETIME.is24;
            jsonDoc["led"]["sensorOn"]              = LED.sensorOn;
            jsonDoc["led"]["delayOffTime"]          = LED.delayOffTime;
            jsonDoc["led"]["buttonOnTime"]          = LED.buttonOnTime;
            jsonDoc["led"]["remoteOnTime"]          = LED.remoteOnTime;
            jsonDoc["screen"]["sensorOn"]           = SCREEN.sensorOn;
            jsonDoc["screen"]["lockInterval"]       = SCREEN.lockInterval;
            jsonDoc["screen"]["scrollInterval"]     = SCREEN.scrollInterval;
            jsonDoc["sync"]["dataSyncInterval"]     = SYNC.dataSyncInterval;
            jsonDoc["sync"]["clockSyncInterval"]    = SYNC.clockSyncInterval;
            jsonDoc["sync"]["weatherSyncInterval"]  = SYNC.weatherSyncInterval;
            jsonDoc["weather"]["unit"]              = WEATHER.unit;
            jsonDoc["weather"]["city"]              = WEATHER.city;
            jsonDoc["weather"]["cityID"]            = WEATHER.cityID;
            serializeJson(jsonDoc, buffer, 1024);
            fs.print(buffer);
            fs.close();
            LOG.print("[info] FS: save local settings(done).\n");
        } else {
            LOG.print("[error] FS: can't open settings.json.\n");
        }
    }
}

/**
 * @brief 初始化文件系统
 */
void initFS() {
    flagFS = SPIFFS.begin();
    if (flagFS) {
        LOG.print("[info] Init SPIFFS(done).\n");
    } else {
        LOG.print("[error] Init SPIFFS(failed).\n");
    }
}

#pragma endregion
#pragma region SCREEN

/**
 * @brief 重置计时器
 * @param {bool} whichOne 选择计时器 true: Lock; false: Scroll
 */
void screenResetTicker(bool whichOne) {
    if (whichOne) {
        tickerScreenLock.detach();
        if (SCREEN.lockInterval != 0) {
            tickerScreenLock.once(SCREEN.lockInterval, screenLockdown);
        }
    } else {
        tickerScreenScroll.detach();
        if (SCREEN.scrollInterval != 0) {
            tickerScreenScroll.once(SCREEN.scrollInterval, screenScroll, true);
        }
    }
}

/**
 * @brief 唤醒屏幕
 */
void screenWakeup() {
    if (SCREEN.isLocked) {
        MENU.isMainUI       = true;
        SCREEN.isLocked     = false;
        SCREEN.currentFrame = 1;
        ui.switchToFrame(SCREEN.currentFrame);
        display.displayOn();
        screenResetTicker(true);
    }
}

/**
 * @brief 息屏
 */
void screenLockdown() {
    if (MENU.isMainUI) {
        MENU.isMainUI   = false;
        SCREEN.isLocked = true;
        display.displayOff();
    }
}

/**
 * @brief 屏幕滚动
 * @param {bool} dir 动画方向
 */
void screenScroll(bool dir) {
    if (!MENU.isMainUI)return;
    SCREEN.currentFrame = (dir) ? ((SCREEN.currentFrame >= 3) ? 1 : SCREEN.currentFrame + 1) : ((SCREEN.currentFrame <= 1) ? 3 : SCREEN.currentFrame - 1);
    ui.setFrameAnimation(SLIDE_LEFT);
    ui.transitionToFrame(SCREEN.currentFrame, dir);
    screenResetTicker(false);
}

/**
 * @brief 屏幕滚动
 * @param {bool} dir 动画方向
 * @param {uint8_t} frame 哪一帧
 */
void screenScrollToFrame(uint8_t dir, uint8_t frame) {
    SCREEN.currentFrame = frame;
    if (dir == 1 || dir == 3) {
        ui.setFrameAnimation(SLIDE_LEFT);
        ui.transitionToFrame(SCREEN.currentFrame, dir == 1);
    } else {
        ui.setFrameAnimation(SLIDE_UP);
        ui.transitionToFrame(SCREEN.currentFrame, dir == 2);
    }
}

#pragma endregion
#pragma region Sensor

/**
 * @brief 读取SR602
 */
void sr602Read() {
    if (digitalRead(GPIO_SR602) == HIGH) {
        if (LED.sensorOn && LED.state == 0) {
            LED.state = 1;
            ledSetState(true, 0);
        }
        if (SCREEN.sensorOn) {
            screenWakeup();
        }
    } else if (LED.sensorOn && LED.state == 1) {
        LED.state = 0;
        if (LED.delayOffTime) {
            ledSetState(false, LED.delayOffTime);
        } else {
            ledOFF();
        }
    }
}

/**
 * @brief 读取温湿度
 */
void dht11Read() {
    dht11Temp = dht.readTemperature(!WEATHER.unit);
    dht11Humi = dht.readHumidity();
}

#pragma endregion
#pragma region LED

/**
 * @brief 关闭LED
 */
void ledOFF() {
    LED.state = 0;
    if (digitalRead(GPIO_LED) != LOW)digitalWrite(GPIO_LED, LOW);
    LOG.print("[info] LED: Turn Off.\n");
}

/**
 * @brief 开启LED
 */
void ledON() {
    if (digitalRead(GPIO_LED) != HIGH)digitalWrite(GPIO_LED, HIGH);
    LOG.print("[info] LED: Turn On.\n");
}

/**
 * @brief LED状态获取
 */
bool ledGetState() {
    return digitalRead(GPIO_LED) == HIGH;
}

/**
 * @brief LED状态设置
 * @param {bool} status 状态(true: 亮, false: 灭)
 * @param {uint32_t} time 持续时间
 */
void ledSetState(bool state, uint32_t time) {
    if (time == 0) {
        if (state) {
            ledON();
        } else {
            ledOFF();
        }
    } else {
        if (state) {
            ledON();
            tickerLED.once(time, ledOFF);
        } else {
            tickerLED.once(time, ledOFF);
        }
    }
}

#pragma endregion
#pragma region Other

/**
 * @brief 计算当月最大天数
 * @retval {uint8_t} 当月最大天数
 */
uint8_t calcDays() {
    if (DATETIME.month == 2) {
        if ((DATETIME.year % 4 == 0 && DATETIME.year % 100 != 0) || (DATETIME.year % 400 == 0)) return 29;
        else return 28;
    } else if (DATETIME.month == 4 || DATETIME.month == 6 || DATETIME.month == 9 || DATETIME.month == 11) {
        return 30;
    } else return 31;
}

/**
 * @brief 固件远程升级
 * @retval {uint8_t} 升级状态
 */
void systemUpgrade() {
    WiFiClient  upgradeClient;
    t_httpUpdate_return ret = ESPhttpUpdate.update(upgradeClient, upgradeUrl);
    switch (ret){
    case HTTP_UPDATE_OK:
        LOG.print("[info] System: Upgrade successed.\n");
        break;
    case HTTP_UPDATE_NO_UPDATES:
        LOG.print("[info] System: No upgrade.\n");
        break;
    case HTTP_UPDATE_FAILED:
        LOG.print("[error] System: Upgrade failed.\n");
        break;
    }
}

/**
 * @brief 自动更新
 */
void systemAutoUpgrade() {
    if (upgradeVersion > SKETCH_VERSION && upgradeUrl != "" && autoUpgrade) {
        LOG.printf("Upgrade_Version: %d\n", upgradeVersion);
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI.wifiSSID, WIFI.wifiPASS);
        while (WiFi.status() != WL_CONNECTED) {
            wifiConnect();
            delay(10000);
        }
        LOG.print("[info] System: Starting upgrade...\n");
        systemUpgrade();
    }
}

/**
 * @brief 日期时间计时循环
 */
void clockLoop() {
    DATETIME.second++;
    if (DATETIME.second == 60) {
        DATETIME.second = 0, DATETIME.minute++;
        if (DATETIME.minute == 60) {
            DATETIME.minute = 0, DATETIME.hour++;
            if (DATETIME.hour == 24) {
                DATETIME.hour = 0, DATETIME.day++;
                if (DATETIME.day == calcDays() + 1) {
                    DATETIME.day = 1, DATETIME.month++;
                    if (DATETIME.month == 13) {
                        DATETIME.month = 1, DATETIME.year++;
                    }
                }
            }
        }
    }
}

/**
 * @brief 初始化GPIO和外围器件(SR602, DHT11)
 */
void initGPIO() {
    /* 初始化按钮 */
    pinMode(GPIO_LBUTTON, INPUT_PULLUP);
    pinMode(GPIO_RBUTTON, INPUT_PULLUP);
    /* 注册按钮中断函数 */
    attachInterrupt(digitalPinToInterrupt(GPIO_LBUTTON), _leftButtonItr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(GPIO_RBUTTON), _rightButtonItr, CHANGE);

    /* 初始化传感器SR602 */
    pinMode(GPIO_SR602, INPUT);

    /* 初始化传感器DHT11 */
    pinMode(GPIO_DHT11, INPUT);
    dht.begin();

    /* 初始化LED管脚 */
    pinMode(GPIO_LED, OUTPUT);

    LOG.print("[info] GPIO: Init done.\n");
}

#pragma endregion

void setup() {
    LOG.begin(115200);
    LOG.printf("\nSKETCH_VERSION: %d\n", SKETCH_VERSION);
    initFS();
    configLoad();
    settingsLoad();
    systemAutoUpgrade();
    initUI();
    initGPIO();
    initMQTT();
    initRequest();
    initWiFi();
    tickerClock.attach(1, clockLoop);
    tickerSR602.attach_ms(100, sr602Read);
}

void loop() {
    static uint32_t dt = 0;
    int remainingTimeBudget = ui.update();
    if (remainingTimeBudget > 0) {
        if (!initFlag) {
            if (dt == 0) {
                dt = millis();
            } else if (WIFI.isConnected || millis() - dt >= MAX_TIMEOUT) {
                initFlag = true;
                MENU.isMainUI = true;
                SCREEN.currentFrame = 1;
                ui.switchToFrame(SCREEN.currentFrame);
            }
        } else {
            if (millis() - dt > 30000 && WiFi.status() != WL_CONNECTED) {
                wifiConnect();
                dt = millis();
            }
        }
        dht11Read();
        delay(remainingTimeBudget);
    }
}
