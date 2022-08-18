/*
 * @Description 
 * @Version 2.0
 * @Author MoFChen mofchen@mofchen.com
 * @Date 2022-08-17 18:42:55
 * @LastEditors MoFChen mofchen@mofchen.com
 * @LastEditTime 2022-08-18 23:32:57
 */
#ifndef _DESKTOP_CLOCK_
#define _DESKTOP_CLOCK_s

#define SKETCH_VERSION      1305

#define WIFI_SSID           "Router-1234"
#define WIFI_PASS           "mypassword"

#define MQTT_HOST           "www.eflystudio.com"
#define MQTT_PORT           1883
#define MQTT_SUB_CONFIG     "esp_device/config"
#define MQTT_SUB_CONTROL    "esp_device/control"
#define MQTT_SUB_DATA       "esp_device/data"
#define MQTT_SUB_SETTINGS   "esp_device/settings"
#define MQTT_SUB_STATUS     "esp_device/status"
#define MQTT_SUB_UPGRADE    "esp_device/upgrade"
#define MQTT_SUB_SERVER     "server/status"

#define API_TIME_URL        "http://worldtimeapi.org/api/ip"
#define API_WEATHER_URL     "http://www.eflystudio.cn/api/weather.php"
#define API_WEATHER_KEY     "3264*********************608b"

#define GPIO_LBUTTON    D1
#define GPIO_RBUTTON    D2
#define GPIO_OLED_SDA   D4
#define GPIO_OLED_SCL   D5
#define GPIO_SR602      D6
#define GPIO_DHT11      D7
#define GPIO_LED        D8

#define MAX_TIMEOUT             10000   // 最大超时时间
#define RETRY_INTERVAL          3       // 重连间隔时间
#define MAX_FAIL_COUNTS         5       // 最大尝试次数
#define LONG_CLICK_TIME         250     // 长按最短时间
#define MIN_SYNC_INTERVAL       3       // 最小数据同步间隔
#define SYNC_TIME_INTERVAL      15      // 时钟同步间隔
#define SYNC_WEATHER_INTERVAL   15      // 天气同步间隔

#pragma region STRUCT

/* LED 结构体 */
typedef struct LED {
    /* LED 感应亮起功能 */
    bool        sensorOn;
    /* LED 延迟关闭时间 */
    uint16_t    delayOffTime;
    /* LED 按钮开启时间 */
    uint16_t    buttonOnTime;
    /* LED 远程开启时间 */
    uint16_t    remoteOnTime;
    /* LED 内部状态 */
    uint8_t     state;
} LED_S;

/* 日期时间结构体 */
typedef struct DATETIME
{
    /* true:显示为24小时制; false:显示为12小时制 */
    bool        is24;
    /* 年 */
    uint16_t    year;
    /* 月 */
    uint8_t     month;
    /* 日 */
    uint8_t     day;
    /* 小时(24小时制) */
    uint8_t     hour;
    /* 分钟 */
    uint8_t     minute;
    /* 秒 */
    uint8_t     second;
} DATETIME_S;

/* 天气信息结构体 */
typedef struct WEATHER {
    /* 温度单位。true:摄氏度; false:华氏度 */
    bool        unit;
    /* 温度 */
    float       temp;
    /* 降水量 */
    float       precip;
    /* 湿度 */
    uint8_t     humi;
    /* 风级 */
    uint8_t     windScale;
    /* 天气图标 */
    uint16_t    icon;
    /* 压力 */
    uint16_t    pressure;
    /* 城市 */
    String      city;
    /* 城市ID */
    String      cityID;
    /* 天气文本 */
    String      text;
    /* 风向 */
    String      wind;
} WEATHER_S;

/* 同步设置结构体 */
typedef struct SYNC {
    /* 数据同步间隔。最小值为5秒。只有云端在线时才进行数据上传。 */
    uint8_t     dataSyncInterval;
    /* 数据同步间隔。最小值为1。单位为2分钟。 */
    uint32_t    clockSyncInterval;
    /* 数据同步间隔。最小值为1。单位为2分钟。 */
    uint32_t    weatherSyncInterval;
    /* 失败次数 */
    uint8_t     failedCount;
    /* 服务器在线状态。true: online, false: offline */
    bool        serverState;
    /* 是否已连接MQTT */
    bool        mqttIsConnected;
    /* MQTT Broker Port */
    uint16_t    mqttPort;
    /* MQTT Broker Domain */
    String      mqttHost;
} SYNC_S;

/* WIFI设置结构体 */
typedef struct WIFI {
    /* 是否已连接WiFi */
    bool        isConnected;
    /* 超时时间 */
    uint8_t     timeout;
    /* WiFi SSID */
    String      wifiSSID;
    /* WiFi Password */
    String      wifiPASS;
} WIFI_S;

/* 屏幕设置结构体 */
typedef struct SCREEN {
    /* 屏幕感应唤醒功能 */
    bool        sensorOn;
    /* 是否已息屏 */
    bool        isLocked;
    /* 轮屏间隔时间 */
    uint16_t    scrollInterval;
    /* 息屏时间 */
    uint16_t    lockInterval;
    /* 上一页面 */
    uint8_t     lastFrame;
    /* 当前帧 */
    uint8_t     currentFrame;
} SCREEN_S;

/* 菜单结构体 */
typedef struct MENU {
    /* 是否为主界面 */
    bool        isMainUI;
    /* 菜单层级 */
    uint8_t     level;
    /* 预留变量 */
    uint16_t    reserved;
    /* 当前菜单索引 */
    uint16_t    current[3];
} MENU_S;

#pragma endregion
#pragma region UI

/**
 * @brief 绘制UI覆盖层
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 */
void uiOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);

/**
 * @brief 绘制小电视加载动画
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameGIF(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

/**
 * @brief 绘制日期和时钟页面
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameDatetime(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

/**
 * @brief 绘制温湿度计页面
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameHygrothermograph(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

/**
 * @brief 绘制天气页面
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameWeather(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

/**
 * @brief 绘制主菜单页面
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameMenu(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

/**
 * @brief 绘制子菜单页面
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameSubmenu(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

/**
 * @brief 绘制设置项
 * @param {OLEDDisplay} *display
 * @param {OLEDDisplayUiState} *state
 * @param {int16_t} x
 * @param {int16_t} y
 */
void uiDrawFrameItem(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

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
void uiDrawItemCheck(OLEDDisplay *display, int16_t x, int16_t y, const char *title, const char value[][24], uint8_t value_len, uint8_t current);

/**
 * @brief 绘制数值设置项
 * @param {OLEDDisplay} *display
 * @param {int16_t} x
 * @param {int16_t} y
 * @param {char} *title 标题
 * @param {uint8_t} value 当前数值
 * @param {char} *units 单位
 */
void uiDrawItemNumber(OLEDDisplay *display, int16_t x, int16_t y, const char *title, uint16_t value, const char *units);

/**
 * @brief 绘制日期时间设置项
 * @param {OLEDDisplay} *display
 * @param {int16_t} x
 * @param {int16_t} y
 * @param {char} *title
 * @param {uint8_t} current
 * @param {bool} state
 */
void uiDrawItemDatetime(OLEDDisplay *display, int16_t x, int16_t y, const char *title, uint8_t current, bool state);

/**
 * @brief 绘制升级页面
 * @param {OLEDDisplay} *display
 * @param {int16_t} x
 * @param {int16_t} y
 * @param {uint16_t} state 升级状态
 */
void uiDrawItemUpgrade(OLEDDisplay *display, int16_t x, int16_t y, uint16_t state);

/**
 * @brief 初始化屏幕和UI
 */
void initUI();

#pragma endregion
#pragma region WIFI

/**
 * @brief WiFi连接成功回调
 * @param {WiFiEventStationModeGotIP&} event
 */
void wifiOnConnect(const WiFiEventStationModeGotIP& event);

/**
 * @brief WiFi断开连接回调
 * @param {WiFiEventStationModeDisconnected&} event
 */
void wifiOnDisconnect(const WiFiEventStationModeDisconnected& event);

/**
 * @brief 连接WiFi
 */
void wifiConnect();

/**
 * @brief 初始化WiFi连接
 */
void initWiFi();

#pragma endregion
#pragma region MQTT

/**
 * @brief MQTT连接成功回调
 * @param {bool} sessionPresent 持久性会话
 */
void mqttOnConnect(bool sessionPresent);

/**
 * @brief MQTT断开连接回调
 * @param {AsyncMqttClientDisconnectReason} reason 连接断开代码
 */
void mqttOnDisconnect(AsyncMqttClientDisconnectReason reason);

/**
 * @brief MQTT接受消息回调
 * @param {char*} topic 主题
 * @param {char*} payload 消息内容
 * @param {AsyncMqttClientMessageProperties&} properties 消息属性(qos, dup, retain)
 * @param {size_t&} len 消息长度
 * @param {size_t&} index
 * @param {size_t&} total
 */
void mqttOnReceive(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, const size_t& len, const size_t& index, const size_t& total);

/**
 * @brief MQTT发布消息回调
 * @param {uint16_t&} packetId 消息标识
 */
void mqttOnPublish(const uint16_t& packetId);

/**
 * @brief 尝试连接 MQTT Broker
 */
void mqttConnect();

/**
 * @brief 通过MQTT发送温湿度数据
 */
void mqttDataSend();

/**
 * @brief 通过MQTT发送用户设置
 */
void mqttSettingsSend();

/**
 * @brief 初始化MQTT
 */
void initMQTT();

#pragma endregion
#pragma region HTTP

/**
 * @brief 发送HTTP请求网络时间
 */
void requestTimeSend();

/**
 * @brief 接受HTTP响应回调，并解析时间信息
 * @param {void*} optParm
 * @param {AsyncHTTPRequest*} request
 * @param {int} readyState
 */
void requestTimeOnReceive(void* optParm, AsyncHTTPRequest* request, int readyState);

/**
 * @brief 发送HTTP请求天气信息
 */
void requestWeatherSend();

/**
 * @brief 接受HTTP响应回调，并解析天气信息
 * @param {void*} optParm
 * @param {AsyncHTTPRequest*} request
 * @param {int} readyState
 * @retval 
 */
void requestWeatherOnReceive(void* optParm, AsyncHTTPRequest* request, int readyState);

/**
 * @brief 初始化HTTP异步请求
 */
void initRequest();

#pragma endregion
#pragma region BUTTON

/**
 * @brief 左按钮中断处理函数
 */
ICACHE_RAM_ATTR void _leftButtonItr();

/**
 * @brief 右按钮中断处理函数
 */
ICACHE_RAM_ATTR void _rightButtonItr();

/**
 * @brief 左按钮短按处理函数
 */
void lButtonShortClick();

/**
 * @brief 右按钮短按处理函数
 */
void rButtonShortClick();

/**
 * @brief 左按钮长按处理函数
 */
void lButtonLongClick();

/**
 * @brief 右按钮长按处理函数
 */
void rButtonLongClick();

#pragma endregion
#pragma region SPIFFS

/**
 * @brief 加载Flash中的系统配置
 */
void configLoad();

/**
 * @brief 保存系统配置到Flash
 */
void configSave();

/**
 * @brief 加载Flash中的用户设置
 */
void settingsLoad();

/**
 * @brief 保存用户设置到Flash
 */
void settingsSave();

/**
 * @brief 初始化文件系统
 */
void initFS();

#pragma endregion
#pragma region SCREEN

/**
 * @brief 重置计时器
 * @param {bool} whichOne 选择计时器 true: Lock; false: Scroll
 */
void screenResetTicker(bool whichOne);

/**
 * @brief 唤醒屏幕
 */
void screenWakeup();

/**
 * @brief 息屏
 */
void screenLockdown();

/**
 * @brief 屏幕滚动
 * @param {bool} dir 动画方向
 */
void screenScroll(bool dir);

/**
 * @brief 屏幕滚动
 * @param {bool} dir 动画方向
 * @param {uint8_t} frame 哪一帧
 */
void screenScrollToFrame(uint8_t dir, uint8_t frame);

#pragma endregion
#pragma region Sensor

/**
 * @brief 读取SR602
 */
void sr602Read();

/**
 * @brief 读取温湿度
 */
void dht11Read();

#pragma endregion
#pragma region LED

/**
 * @brief 关闭LED
 */
void ledOFF();

/**
 * @brief 开启LED
 */
void ledON();

/**
 * @brief LED状态获取
 */
bool ledGetState();

/**
 * @brief LED状态设置
 * @param {bool} status 状态(true: 亮, false: 灭)
 * @param {uint32_t} time 持续时间
 */
void ledSetState(bool state, uint32_t time);

#pragma endregion
#pragma region Other

/**
 * @brief 计算当月最大天数
 * @retval {uint8_t} 当月最大天数
 */
uint8_t calcDays();

/**
 * @brief 固件远程升级
 * @retval {uint8_t} 升级状态
 */
void systemUpgrade();

/**
 * @brief 自动更新
 */
void systemAutoUpgrade();

/**
 * @brief 日期时间计时循环
 */
void clockLoop();

/**
 * @brief 初始化GPIO和外围器件(SR602, DHT11)
 */
void initGPIO();

#pragma endregion

#endif
