/* =============================================================================
 * @brief   DC Electronic Load(v4.1)
 * =============================================================================*/

#include "main.h"

/* USER CODE BEGIN Includes */
#include "ssd1306.h"
#include "fonts.h"
#include "mcp4725.h"
#include "ads1115.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PD */
/* Timing -------------------------------------------------------------------- */
#define UART_TX_INTERVAL_MS         500UL
#define BUZZER_DURATION_MS           30UL
#define DEBOUNCE_MS                 400UL
#define ADS_POLL_INTERVAL_MS          2UL
#define DISPLAY_REFRESH_MS          250UL
#define TEMP_READ_INTERVAL_MS      1000UL
#define MAH_UPDATE_INTERVAL_MS     1000UL
#define FAN_RPM_WINDOW_MS          1000UL
#define FAN_PWM_UPDATE_MS           500UL
#define NVM_SAVE_INTERVAL_MS      60000UL

/* NTC thermistor ------------------------------------------------------------ */
#define NTC_PULLUP_OHMS             9100.0f
#define NTC_NOMINAL_OHMS          100000.0f
#define NTC_T0_KELVIN               298.15f
#define NTC_BETA                   3435.0f
#define ADC_FULL_SCALE             4095.0f

/* Default calibration ------------------------------------------------------- */
#define VOLT1_CAL_DEFAULT            6.1648f
#define VOLT2_CAL_DEFAULT            6.1648f
#define CURR_CAL_DEFAULT             1.1098f

/* DAC / load ---------------------------------------------------------------- */
#define VDD_MV                      5000UL
#define MAX_CURRENT_MA              5000UL
#define DAC_FULL_SCALE              4095UL

/* Battery test -------------------------------------------------------------- */
#define BATT_DEFAULT_CURRENT_MA     1500UL
#define BATT_MAX_CAPACITY_MAH      10000UL

/* Fan ----------------------------------------------------------------------- */
#define FAN_TEMP_MIN_C                35.0f
#define FAN_TEMP_MAX_C                65.0f
#define FAN_PWM_MIN                  800UL
#define FAN_PWM_MAX                 2880UL
#define FAN_FAULT_TEMP_THRESH_C       10.0f
#define FAN_FAULT_ALARM_MS           5000UL
#define FAN_PULSES_PER_REV              2UL

/* Encoder ------------------------------------------------------------------- */
#define ENCODER_DIVIDER                 4

/* NVM ----------------------------------------------------------------------- */
#define FLASH_SAVE_ADDR         0x0800F800UL
#define FLASH_SAVE_MAGIC        0xDEADBEEFUL

/* Volt2 fault threshold ----------------------------------------------------- */
#define VOLT2_BAD_THRESH                5U

/* WiFi / UART receive ------------------------------------------------------- */
#define WIFI_IP_BUF_LEN                20U
#define UART_RX_BUF_LEN                32U
/* USER CODE END PD */

/* USER CODE BEGIN PTD */
typedef enum
{
    SCREEN_MODE_SELECT = 0,
    SCREEN_SUBMENU,
    SCREEN_SET_VALUE,
    SCREEN_RUNNING,
    SCREEN_TEMPERATURES,
    SCREEN_FAN_CONTROL,
    SCREEN_CALIBRATION,
    SCREEN_CAL_ENTRY,
    SCREEN_WIFI
} ScreenState_t;

typedef enum { MODE_CC = 0, MODE_CP, MODE_BATT_TEST } LoadMode_t;

typedef enum
{
    SUBMENU_SET   = 0,
    SUBMENU_START,
    SUBMENU_BACK,
    SUBMENU_COUNT
} SubMenuItem_t;

typedef enum { BATT_EDIT_CURRENT = 0, BATT_EDIT_VOLTAGE } BattEditField_t;
typedef enum { FAN_MODE_AUTO     = 0, FAN_MODE_MANUAL   } FanMode_t;

typedef enum
{
    CAL_ITEM_V1_TRUE = 0,
    CAL_ITEM_V2_TRUE,
    CAL_ITEM_I_TRUE,
    CAL_ITEM_V1_RES,
    CAL_ITEM_V2_RES,
    CAL_ITEM_I_RES,
    CAL_ITEM_SAVE,
    CAL_ITEM_RESET,
    CAL_ITEM_BACK,
    CAL_ITEM_COUNT
} CalMenuItem_t;

typedef enum
{
    CAL_ENTRY_V1_TRUE = 0,
    CAL_ENTRY_V2_TRUE,
    CAL_ENTRY_I_TRUE,
    CAL_ENTRY_V1_RTOP,
    CAL_ENTRY_V1_RBOT,
    CAL_ENTRY_V2_RTOP,
    CAL_ENTRY_V2_RBOT,
    CAL_ENTRY_I_SHUNT,
    CAL_ENTRY_I_GAIN
} CalEntryType_t;

typedef enum
{
    WIFI_ITEM_RESET_DATA = 0,
    WIFI_ITEM_RESET_WIFI,
    WIFI_ITEM_BACK,
    WIFI_ITEM_COUNT
} WifiMenuItem_t;

typedef struct
{
    ScreenState_t screen;
    LoadMode_t    loadMode;
    int8_t        menuIndex;
    uint8_t       displayNeedsUpdate;
} AppState_t;

typedef struct
{
    int8_t   digits[6];
    int8_t   cursor;
    uint32_t value;
} DigitEditor_t;

typedef struct
{
    uint32_t voltage_mV;
    uint32_t current_mA;
    uint32_t power_mW;
    uint32_t capacity_mAh;
} Measurements_t;

typedef struct
{
    uint32_t        dischargeCurrent_mA;
    uint32_t        cutoffVoltage_mV;
    BattEditField_t editField;
    float           mAh_accumulator;
    uint8_t         finished;
} BatteryTest_t;

typedef struct
{
    uint32_t stopTick;
    uint8_t  active;
} Buzzer_t;

typedef struct
{
    FanMode_t mode;
    uint8_t   manualPercent;
    int8_t    menuIndex;
    uint8_t   editingPercent;
} FanControl_t;

typedef struct
{
    float   volt1Scale;
    int32_t volt1Offset_mV;
    float   volt2Scale;
    int32_t volt2Offset_mV;
    float   currScale;
    int32_t currOffset_mA;
} Calibration_t;

typedef struct
{
    int8_t         digits[5];
    int8_t         cursor;
    uint8_t        numDigits;
    uint8_t        intDigits;
    CalEntryType_t type;
    uint32_t       tempVal;
} CalEntry_t;

typedef struct
{
    int8_t menuIndex;
} CalScreen_t;

typedef struct
{
    uint32_t magic;
    uint32_t capacity_mAh;
    uint32_t mAh_milli;
    uint32_t dischargeCurrent_mA;
    uint32_t cutoffVoltage_mV;
    uint32_t testFlags;
    uint32_t volt1Scale_x1000;
    int32_t  volt1Offset_mV;
    uint32_t volt2Scale_x1000;
    int32_t  volt2Offset_mV;
    uint32_t currScale_x1000;
    int32_t  currOffset_mA;
    uint32_t checksum;
} NvmData_t;

typedef struct
{
    int8_t menuIndex;
    char   ipAddr[WIFI_IP_BUF_LEN];
} WifiScreen_t;
/* USER CODE END PTD */

/* USER CODE BEGIN PV */
static const char * const SUBMENU_LABELS[SUBMENU_COUNT] =
{
    "Set Value", "Start", "Back"
};

static const char * const MAIN_MENU_ITEMS[] =
{
    "Constant Current",
    "Constant Power",
    "Battery Test",
    "Temperatures",
    "Fan Control",
    "Calibration",
    "WiFi"
};
#define MAIN_MENU_ITEM_COUNT    7U

static const char * const FAN_MENU_LABELS[] = { "Automatic", "Manual", "Back" };
#define FAN_MENU_COUNT          3U

static const char * const CAL_MENU_LABELS[CAL_ITEM_COUNT] =
{
    "V1 True Value",
    "V2 True Value",
    "I  True Value",
    "V1 R_top/R_bot",
    "V2 R_top/R_bot",
    "I  Shunt/Gain",
    "Save to Flash",
    "Reset Default",
    "Back"
};

static const char * const WIFI_MENU_LABELS[WIFI_ITEM_COUNT] =
{
    "Reset Data",
    "Reset WiFi",
    "Back"
};

static AppState_t    app    = { SCREEN_MODE_SELECT, MODE_CC, 0, 1 };
static DigitEditor_t editor = { {0}, 0, 0 };

static Measurements_t meas =
{
    .voltage_mV   = 12500,
    .current_mA   =  4950,
    .power_mW     = 61875,
    .capacity_mAh =     0
};

static BatteryTest_t batt =
{
    .dischargeCurrent_mA = BATT_DEFAULT_CURRENT_MA,
    .cutoffVoltage_mV    = 3000UL,
    .editField           = BATT_EDIT_CURRENT,
    .mAh_accumulator     = 0.0f,
    .finished            = 0
};

static Buzzer_t     buzzer  = { 0, 0 };
static FanControl_t fanCtrl = { FAN_MODE_AUTO, 50, 0, 0 };

static Calibration_t cal =
{
    .volt1Scale     = VOLT1_CAL_DEFAULT,
    .volt1Offset_mV = 0,
    .volt2Scale     = VOLT2_CAL_DEFAULT,
    .volt2Offset_mV = 0,
    .currScale      = CURR_CAL_DEFAULT,
    .currOffset_mA  = 0
};

static CalScreen_t calScreen = { 0 };
static CalEntry_t  calEntry  = { {0}, 0, 4, 1, CAL_ENTRY_V1_TRUE, 0 };

static float    lastRawVolt1 = 0.0f;
static float    lastRawVolt2 = 0.0f;
static float    lastRawCurr  = 0.0f;

static uint32_t lastVolt1_mV = 0;
static uint32_t lastVolt2_mV = 0;

static volatile float    temp1_C   = 0.0f;
static volatile float    temp2_C   = 0.0f;
static volatile float    fanTemp_C = 0.0f;
static volatile uint32_t fanRPM    = 0;

static int16_t lastEncoderRaw = 0;

static WifiScreen_t wifiScreen = { 0, "19" };

static int8_t savedMainMenuIndex = 0;

static uint8_t rxByte    = 0;
static char    rxLineBuf[UART_RX_BUF_LEN] = { 0 };
static uint8_t rxLineIdx = 0;
/* USER CODE END PV */

/* HAL peripheral handles — generated by CubeMX, do NOT move these */
ADC_HandleTypeDef  hadc1, hadc2;
DMA_HandleTypeDef  hdma_adc1;
I2C_HandleTypeDef  hi2c1, hi2c2;
TIM_HandleTypeDef  htim1, htim2, htim3, htim4;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PFP */
/* Peripheral helpers */
static uint16_t ADC_ReadChannel(ADC_HandleTypeDef *hadc, uint32_t channel);
static float    NTC_ToTemperatureCelsius(uint16_t adcRaw);

/* Buzzer */
static void Buzzer_Trigger(void);
static void Buzzer_Service(void);

/* Digit editor */
static uint8_t  DigitEditor_GetNumDigits(void);
static uint32_t DigitEditor_BuildValue(void);
static void     DigitEditor_Clamp(int8_t changedDigit);
static void     DigitEditor_MoveCursor(int8_t dir);
static void     DigitEditor_LoadBattField(BattEditField_t field);
static void     DigitEditor_SaveBattField(BattEditField_t field);

/* Calibration entry */
static void     CalEntry_Setup(CalEntryType_t type, uint32_t tempVal);
static void     CalEntry_Confirm(void);
static void     CalEntry_MoveCursor(int8_t dir);

/* Display */
static void Display_DrawRow(uint8_t y, const char *text, uint8_t selected);
static void Display_DrawDigitEditor(void);
static void Display_DrawCalEditor(void);
static void Display_UpdateMainMenu(void);
static void Display_UpdateSubmenu(void);
static void Display_UpdateSetValue(void);
static void Display_UpdateRunning(void);
static void Display_UpdateTemperatures(void);
static void Display_UpdateFanControl(void);
static void Display_UpdateCalMenu(void);
static void Display_UpdateCalEntry(void);
static void Display_UpdateWifi(void);
static void Display_Refresh(void);

/* Sensors */
static void Sensor_ServiceTemperatures(void);
static void Sensor_ServiceADS1115(void);
static void Sensor_ServiceFanRPM(void);

/* Thermal */
static void Thermal_ServiceFan(void);
static void Thermal_ServiceFanAlarm(void);

/* Load control */
static uint16_t Load_ComputeDAC(void);
static void     Load_UpdateDAC(void);
static void     Load_ServiceCapacity(void);
static void     Load_CheckBattCutoff(void);

/* Encoder */
static void Encoder_Service(void);

/* NVM */
static uint32_t NVM_Checksum(const NvmData_t *d);
static void     NVM_Save(void);
static void     NVM_Load(void);

/* UART */
static void UART_SendData(void);
static void UART_ParseRxLine(const char *line);

/* ESP */
void ESP_HardwareReset(void);

/* Utility */
static void FloatToString(char *str, float value, int decimalPlaces);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
static void FloatToString(char *str, float value, int decimalPlaces)
{
    char *ptr = str;

    if (value < 0.0f)
    {
        *ptr++ = '-';
        value  = -value;
    }

    int intPart = (int)value;
    itoa(intPart, ptr, 10);
    ptr += strlen(ptr);

    if (decimalPlaces > 0)
    {
        *ptr++ = '.';
        float fracPart = value - (float)intPart;

        for (int i = 0; i < decimalPlaces; i++)
        {
            fracPart *= 10.0f;
            int digit = (int)fracPart;
            *ptr++    = (char)('0' + digit);
            fracPart -= (float)digit;
        }
    }

    *ptr = '\0';
}

/* ---- NVM ------------------------------------------------------------------ */

static uint32_t NVM_Checksum(const NvmData_t *d)
{
    const uint32_t *p   = (const uint32_t *)d;
    uint32_t        sum = 0U;
    uint32_t        n   = (sizeof(NvmData_t) / 4U) - 1U;

    for (uint32_t i = 0U; i < n; i++) { sum += p[i]; }

    return sum;
}

static void NVM_Save(void)
{
    NvmData_t d;

    d.magic               = FLASH_SAVE_MAGIC;
    d.capacity_mAh        = meas.capacity_mAh;
    d.mAh_milli           = (uint32_t)(batt.mAh_accumulator * 1000.0f);
    d.dischargeCurrent_mA = batt.dischargeCurrent_mA;
    d.cutoffVoltage_mV    = batt.cutoffVoltage_mV;
    d.testFlags           = ((app.screen == SCREEN_RUNNING &&
                              app.loadMode == MODE_BATT_TEST) ? 1U : 0U) |
                            (batt.finished ? 2U : 0U);
    d.volt1Scale_x1000    = (uint32_t)(cal.volt1Scale * 1000.0f + 0.5f);
    d.volt1Offset_mV      = cal.volt1Offset_mV;
    d.volt2Scale_x1000    = (uint32_t)(cal.volt2Scale * 1000.0f + 0.5f);
    d.volt2Offset_mV      = cal.volt2Offset_mV;
    d.currScale_x1000     = (uint32_t)(cal.currScale  * 1000.0f + 0.5f);
    d.currOffset_mA       = cal.currOffset_mA;
    d.checksum            = NVM_Checksum(&d);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef ei;
    uint32_t pageError = 0U;
    ei.TypeErase   = FLASH_TYPEERASE_PAGES;
    ei.PageAddress = FLASH_SAVE_ADDR;
    ei.NbPages     = 1U;
    HAL_FLASHEx_Erase(&ei, &pageError);

    const uint32_t *src      = (const uint32_t *)&d;
    uint32_t        numWords = sizeof(NvmData_t) / 4U;

    for (uint32_t i = 0U; i < numWords; i++)
    {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          FLASH_SAVE_ADDR + (i * 4U),
                          (uint64_t)src[i]);
    }

    HAL_FLASH_Lock();
}

static void NVM_Load(void)
{
    const NvmData_t *s = (const NvmData_t *)FLASH_SAVE_ADDR;

    if (s->magic    != FLASH_SAVE_MAGIC) { return; }
    if (s->checksum != NVM_Checksum(s))  { return; }

    if (s->volt1Scale_x1000 > 0U) { cal.volt1Scale = (float)s->volt1Scale_x1000 / 1000.0f; }
    if (s->volt2Scale_x1000 > 0U) { cal.volt2Scale = (float)s->volt2Scale_x1000 / 1000.0f; }
    if (s->currScale_x1000  > 0U) { cal.currScale  = (float)s->currScale_x1000  / 1000.0f; }

    cal.volt1Offset_mV = s->volt1Offset_mV;
    cal.volt2Offset_mV = s->volt2Offset_mV;
    cal.currOffset_mA  = s->currOffset_mA;

    if (s->testFlags & 1U)
    {
        batt.dischargeCurrent_mA = s->dischargeCurrent_mA;
        batt.cutoffVoltage_mV    = s->cutoffVoltage_mV;
        batt.mAh_accumulator     = (float)s->mAh_milli / 1000.0f;
        meas.capacity_mAh        = s->capacity_mAh;
        batt.finished            = (s->testFlags & 2U) ? 1U : 0U;
    }
}

/* ---- Calibration entry ---------------------------------------------------- */

static void CalEntry_Setup(CalEntryType_t type, uint32_t tempVal)
{
    calEntry.type    = type;
    calEntry.tempVal = tempVal;
    calEntry.cursor  = 0;

    for (uint8_t i = 0U; i < 5U; i++) { calEntry.digits[i] = 0; }

    switch (type)
    {
        case CAL_ENTRY_V1_TRUE:
        case CAL_ENTRY_V2_TRUE:
            calEntry.numDigits = 5U; calEntry.intDigits = 2U; break;

        case CAL_ENTRY_I_TRUE:
            calEntry.numDigits = 4U; calEntry.intDigits = 1U; break;

        case CAL_ENTRY_V1_RTOP: case CAL_ENTRY_V1_RBOT:
        case CAL_ENTRY_V2_RTOP: case CAL_ENTRY_V2_RBOT:
            calEntry.numDigits = 4U; calEntry.intDigits = 3U; break;

        case CAL_ENTRY_I_SHUNT:
            calEntry.numDigits = 4U; calEntry.intDigits = 4U; break;

        case CAL_ENTRY_I_GAIN:
            calEntry.numDigits = 3U; calEntry.intDigits = 3U; break;

        default: break;
    }
}

static uint32_t CalEntry_BuildValue(void)
{
    uint32_t v = 0U;

    for (uint8_t i = 0U; i < calEntry.numDigits; i++)
    {
        v = v * 10U + (uint32_t)(uint8_t)calEntry.digits[i];
    }

    return v;
}

static void CalEntry_MoveCursor(int8_t dir)
{
    calEntry.cursor += dir;

    if (calEntry.cursor < 0)                           { calEntry.cursor = (int8_t)(calEntry.numDigits - 1U); }
    if (calEntry.cursor >= (int8_t)calEntry.numDigits) { calEntry.cursor = 0; }
}

static void CalEntry_Confirm(void)
{
    uint32_t val = CalEntry_BuildValue();

    switch (calEntry.type)
    {
        case CAL_ENTRY_V1_TRUE:
            if (val > 0U && lastRawVolt1 > 0.001f)
            {
                cal.volt1Scale = (float)val / (lastRawVolt1 * 1000.0f);
            }
            break;

        case CAL_ENTRY_V2_TRUE:
            if (val > 0U && lastRawVolt2 > 0.001f)
            {
                cal.volt2Scale = (float)val / (lastRawVolt2 * 1000.0f);
            }
            break;

        case CAL_ENTRY_I_TRUE:
            if (val > 0U && lastRawCurr > 0.001f)
            {
                cal.currScale = (float)val / (lastRawCurr * 1000.0f);
            }
            break;

        case CAL_ENTRY_V1_RTOP:
            CalEntry_Setup(CAL_ENTRY_V1_RBOT, val);
            app.screen             = SCREEN_CAL_ENTRY;
            app.displayNeedsUpdate = 1;
            return;

        case CAL_ENTRY_V1_RBOT:
            if (val > 0U) { cal.volt1Scale = (float)(calEntry.tempVal + val) / (float)val; }
            break;

        case CAL_ENTRY_V2_RTOP:
            CalEntry_Setup(CAL_ENTRY_V2_RBOT, val);
            app.screen             = SCREEN_CAL_ENTRY;
            app.displayNeedsUpdate = 1;
            return;

        case CAL_ENTRY_V2_RBOT:
            if (val > 0U) { cal.volt2Scale = (float)(calEntry.tempVal + val) / (float)val; }
            break;

        case CAL_ENTRY_I_SHUNT:
            CalEntry_Setup(CAL_ENTRY_I_GAIN, val);
            app.screen             = SCREEN_CAL_ENTRY;
            app.displayNeedsUpdate = 1;
            return;

        case CAL_ENTRY_I_GAIN:
            if (val > 0U && calEntry.tempVal > 0U)
            {
                cal.currScale = 1000.0f / ((float)calEntry.tempVal * (float)val);
            }
            break;

        default: break;
    }

    app.screen             = SCREEN_CALIBRATION;
    app.displayNeedsUpdate = 1;
}

/* ---- Peripheral helpers --------------------------------------------------- */

static uint16_t ADC_ReadChannel(ADC_HandleTypeDef *hadc, uint32_t channel)
{
    ADC_ChannelConfTypeDef cfg =
    {
        .Channel      = channel,
        .Rank         = ADC_REGULAR_RANK_1,
        .SamplingTime = ADC_SAMPLETIME_239CYCLES_5
    };

    if (HAL_ADC_ConfigChannel(hadc, &cfg) != HAL_OK) { return 0U; }

    HAL_ADC_Start(hadc);
    HAL_ADC_PollForConversion(hadc, 10);
    uint16_t v = HAL_ADC_GetValue(hadc);
    HAL_ADC_Stop(hadc);

    return v;
}

static float NTC_ToTemperatureCelsius(uint16_t adcRaw)
{
    if (adcRaw == 0U || adcRaw >= (uint16_t)ADC_FULL_SCALE) { return -99.0f; }

    float rNTC  = NTC_PULLUP_OHMS * ((float)adcRaw / (ADC_FULL_SCALE - (float)adcRaw));
    float tempK = 1.0f / ((1.0f / NTC_T0_KELVIN) +
                          (1.0f / NTC_BETA) * logf(rNTC / NTC_NOMINAL_OHMS));

    return tempK - 273.15f;
}

/* ---- Buzzer --------------------------------------------------------------- */

static void Buzzer_Trigger(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
    buzzer.stopTick = HAL_GetTick() + BUZZER_DURATION_MS;
    buzzer.active   = 1U;
}

static void Buzzer_Service(void)
{
    if (buzzer.active && (HAL_GetTick() >= buzzer.stopTick))
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
        buzzer.active = 0U;
    }
}

/* ---- Digit editor --------------------------------------------------------- */

static uint8_t DigitEditor_GetNumDigits(void)
{
    return (app.loadMode == MODE_CP) ? 6U : 4U;
}

static uint32_t DigitEditor_BuildValue(void)
{
    const int8_t *d = editor.digits;

    if (app.loadMode == MODE_CP)
    {
        return (uint32_t)(d[0]*100000 + d[1]*10000 + d[2]*1000 + d[3]*100 + d[4]*10 + d[5]);
    }

    return (uint32_t)(d[0]*1000 + d[1]*100 + d[2]*10 + d[3]);
}

static void DigitEditor_LoadBattField(BattEditField_t field)
{
    uint32_t val = (field == BATT_EDIT_CURRENT) ? batt.dischargeCurrent_mA
                                                 : batt.cutoffVoltage_mV;
    editor.digits[0] = (int8_t)( val / 1000U);
    editor.digits[1] = (int8_t)((val % 1000U) / 100U);
    editor.digits[2] = (int8_t)((val %  100U) /  10U);
    editor.digits[3] = (int8_t)( val %   10U);
    editor.cursor    = 0;
    editor.value     = val;
}

static void DigitEditor_SaveBattField(BattEditField_t field)
{
    uint32_t val = DigitEditor_BuildValue();

    if (field == BATT_EDIT_CURRENT) { batt.dischargeCurrent_mA = val; }
    else                            { batt.cutoffVoltage_mV    = val; }
}

static void DigitEditor_Clamp(int8_t idx)
{
    int8_t *d = editor.digits;

    if (idx > 0 || app.loadMode == MODE_CP)
    {
        if (d[idx] > 9) { d[idx] = 0; }
        if (d[idx] < 0) { d[idx] = 9; }
    }

    switch (app.loadMode)
    {
        case MODE_CC:
            if (idx == 0) { if (d[0] > 5) { d[0] = 0; } if (d[0] < 0) { d[0] = 5; } }
            if (d[0] == 5) { d[1] = 0; d[2] = 0; d[3] = 0; }
            break;

        case MODE_CP:
            if (idx == 0) { if (d[0] > 2) { d[0] = 0; } if (d[0] < 0) { d[0] = 2; } }
            if (d[0] == 2) { d[1] = 0; d[2] = 0; d[3] = 0; d[4] = 0; d[5] = 0; }
            break;

        case MODE_BATT_TEST:
            if (batt.editField == BATT_EDIT_CURRENT)
            {
                if (idx == 0) { if (d[0] > 5) { d[0] = 0; } if (d[0] < 0) { d[0] = 5; } }
                if (d[0] == 5) { d[1] = 0; d[2] = 0; d[3] = 0; }
            }
            else
            {
                if (idx == 0) { if (d[0] > 4) { d[0] = 2; } if (d[0] < 2) { d[0] = 4; } }
                if (d[0] == 4) { if (d[1] > 2) { d[1] = 2; } if (d[1] == 2) { d[2] = 0; d[3] = 0; } }
                if (d[0] == 2 && d[1] < 5) { d[1] = 5; }
            }
            break;

        default: break;
    }
}

static void DigitEditor_MoveCursor(int8_t dir)
{
    uint8_t n = DigitEditor_GetNumDigits();
    editor.cursor += dir;

    if (editor.cursor < 0)          { editor.cursor = (int8_t)(n - 1U); }
    if (editor.cursor >= (int8_t)n) { editor.cursor = 0; }
}

/* ---- Display helpers ------------------------------------------------------ */

static void Display_DrawRow(uint8_t y, const char *text, uint8_t selected)
{
    if (selected)
    {
        SSD1306_DrawFilledRectangle(0, y - 1, 127, 13, SSD1306_COLOR_WHITE);
        SSD1306_GotoXY(4, y);
        SSD1306_Puts((char *)text, &Font_7x10, SSD1306_COLOR_BLACK);
    }
    else
    {
        SSD1306_GotoXY(4, y);
        SSD1306_Puts((char *)text, &Font_7x10, SSD1306_COLOR_WHITE);
    }
}

static void Display_DrawDigitEditor(void)
{
    uint8_t n      = DigitEditor_GetNumDigits();
    uint8_t dotPos = (app.loadMode == MODE_CP) ? 2U  : 0U;
    uint8_t startX = (app.loadMode == MODE_CP) ? 4U  : 20U;

    for (uint8_t i = 0U; i < n; i++)
    {
        uint8_t x = startX + (i * 14U) + ((i > dotPos) ? 10U : 0U);
        char s[2] = { (char)('0' + editor.digits[i]), '\0' };

        if (i == (uint8_t)editor.cursor)
        {
            SSD1306_DrawFilledRectangle(x - 1, 24, 13, 20, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(x, 25);
            SSD1306_Puts(s, &Font_11x18, SSD1306_COLOR_BLACK);
        }
        else
        {
            SSD1306_GotoXY(x, 25);
            SSD1306_Puts(s, &Font_11x18, SSD1306_COLOR_WHITE);
        }
    }

    uint8_t dotX = (app.loadMode == MODE_CP) ? 46U : 34U;
    SSD1306_GotoXY(dotX, 25);
    SSD1306_Puts(".", &Font_11x18, SSD1306_COLOR_WHITE);

    const char *unit;
    if (app.loadMode == MODE_BATT_TEST)
    {
        unit = (batt.editField == BATT_EDIT_CURRENT) ? "A" : "V";
    }
    else
    {
        unit = (app.loadMode == MODE_CC) ? "A" : "W";
    }

    uint8_t unitX = (app.loadMode == MODE_CP) ? 102U : 92U;
    SSD1306_GotoXY(unitX, 25);
    SSD1306_Puts((char *)unit, &Font_11x18, SSD1306_COLOR_WHITE);
}

static void Display_DrawCalEditor(void)
{
    uint8_t n      = calEntry.numDigits;
    uint8_t intDig = calEntry.intDigits;
    uint8_t hasDot = (intDig < n) ? 1U : 0U;

    uint16_t totalW = (uint16_t)(n * 14U) + (hasDot ? 10U : 0U);
    uint8_t  startX = (uint8_t)((128U - totalW) / 2U);
    uint8_t  xPos   = startX;

    for (uint8_t i = 0U; i < n; i++)
    {
        if (hasDot && i == intDig)
        {
            SSD1306_GotoXY(xPos, 25);
            SSD1306_Puts(".", &Font_11x18, SSD1306_COLOR_WHITE);
            xPos += 10U;
        }

        char s[2] = { (char)('0' + calEntry.digits[i]), '\0' };

        if (i == (uint8_t)calEntry.cursor)
        {
            SSD1306_DrawFilledRectangle(xPos - 1, 24, 13, 20, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(xPos, 25);
            SSD1306_Puts(s, &Font_11x18, SSD1306_COLOR_BLACK);
        }
        else
        {
            SSD1306_GotoXY(xPos, 25);
            SSD1306_Puts(s, &Font_11x18, SSD1306_COLOR_WHITE);
        }

        xPos += 14U;
    }
}

/* ---- Display screen renderers --------------------------------------------- */

static void Display_UpdateMainMenu(void)
{
    SSD1306_GotoXY(30, 0);
    SSD1306_Puts("MAIN MENU", &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_DrawLine(0, 12, 127, 12, SSD1306_COLOR_WHITE);

    uint8_t startIdx = (app.menuIndex > 3) ? (uint8_t)(app.menuIndex - 3) : 0U;

    for (uint8_t i = 0U; i < 4U; i++)
    {
        uint8_t idx = startIdx + i;
        if (idx < MAIN_MENU_ITEM_COUNT)
        {
            Display_DrawRow(16U + (i * 12U), MAIN_MENU_ITEMS[idx],
                            app.menuIndex == (int8_t)idx);
        }
    }
}

static void Display_UpdateSubmenu(void)
{
    const char *lbl;
    switch (app.loadMode)
    {
        case MODE_CC:        lbl = "Current"; break;
        case MODE_CP:        lbl = "Power";   break;
        case MODE_BATT_TEST: lbl = "Battery"; break;
        default:             lbl = "Unknown"; break;
    }

    SSD1306_GotoXY(12, 0);
    SSD1306_Puts((char *)lbl, &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_DrawLine(0, 12, 127, 12, SSD1306_COLOR_WHITE);

    for (uint8_t i = 0U; i < SUBMENU_COUNT; i++)
    {
        Display_DrawRow(18U + (i * 16U), SUBMENU_LABELS[i], app.menuIndex == (int8_t)i);
    }

    if (app.loadMode == MODE_BATT_TEST && meas.capacity_mAh > 0U && !batt.finished)
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "Resume: %lu mAh", meas.capacity_mAh);
        SSD1306_GotoXY(4, 54);
        SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
    }
}

static void Display_UpdateSetValue(void)
{
    char buf[32];

    if (app.loadMode == MODE_BATT_TEST)
    {
        SSD1306_GotoXY(4, 0);
        SSD1306_Puts((batt.editField == BATT_EDIT_CURRENT) ? "SET I_DISCH" : "SET V_CUTOFF",
                     &Font_7x10, SSD1306_COLOR_WHITE);

        uint32_t    ref = (batt.editField == BATT_EDIT_CURRENT) ? batt.cutoffVoltage_mV
                                                                 : batt.dischargeCurrent_mA;
        const char *rl  = (batt.editField == BATT_EDIT_CURRENT) ? "Vco:" : "I:";
        const char *ru  = (batt.editField == BATT_EDIT_CURRENT) ? "V"    : "A";

        snprintf(buf, sizeof(buf), "%s %lu.%03lu%s", rl, ref / 1000UL, ref % 1000UL, ru);
        SSD1306_GotoXY(4, 52);
        SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
    }
    else
    {
        SSD1306_GotoXY(18, 0);
        SSD1306_Puts("SET VALUE", &Font_7x10, SSD1306_COLOR_WHITE);
    }

    SSD1306_DrawLine(0, 12, 127, 12, SSD1306_COLOR_WHITE);
    Display_DrawDigitEditor();
    editor.value = DigitEditor_BuildValue();
}

static void Display_UpdateRunning(void)
{
    char buf[32];
    SSD1306_GotoXY(3, 0);

    if (app.loadMode == MODE_BATT_TEST && batt.finished)
    {
        SSD1306_Puts("BATT TEST: DONE", &Font_7x10, SSD1306_COLOR_WHITE);
    }
    else if (app.loadMode == MODE_BATT_TEST)
    {
        snprintf(buf, sizeof(buf), "I:%lu.%03luA Vc:%lu.%02luV",
                 batt.dischargeCurrent_mA / 1000UL, batt.dischargeCurrent_mA % 1000UL,
                 batt.cutoffVoltage_mV    / 1000UL, (batt.cutoffVoltage_mV % 1000UL) / 10UL);
        SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
    }
    else
    {
        editor.value = DigitEditor_BuildValue();
        uint16_t si  = (uint16_t)(editor.value / 1000U);
        uint16_t sf  = (uint16_t)(editor.value % 1000U);
        const char *u   = (app.loadMode == MODE_CC) ? "A"             : "W";
        const char *fmt = (app.loadMode == MODE_CC) ? "SET: %u.%03u %s"
                                                     : "SET: %03u.%03u %s";
        snprintf(buf, sizeof(buf), fmt, si, sf, u);
        SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
    }

    SSD1306_DrawLine(0, 11, 127, 11, SSD1306_COLOR_WHITE);

    snprintf(buf, sizeof(buf), "V: %2lu.%02lu V",
             meas.voltage_mV / 1000UL, (meas.voltage_mV % 1000UL) / 10UL);
    SSD1306_GotoXY(3, 15);
    SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);

    snprintf(buf, sizeof(buf), "I: %lu.%03lu A",
             meas.current_mA / 1000UL, meas.current_mA % 1000UL);
    SSD1306_GotoXY(3, 27);
    SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);

    snprintf(buf, sizeof(buf), "P: %3lu.%02lu W",
             meas.power_mW / 1000UL, (meas.power_mW % 1000UL) / 10UL);
    SSD1306_GotoXY(3, 39);
    SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);

    snprintf(buf, sizeof(buf), "E: %lu mAh", meas.capacity_mAh);
    SSD1306_GotoXY(3, 51);
    SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);

    if (fanTemp_C <= 0.0f) { snprintf(buf, sizeof(buf), "T:---"); }
    else                   { snprintf(buf, sizeof(buf), "T:%dC", (int)fanTemp_C); }
    SSD1306_GotoXY(82, 39);
    SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);

    snprintf(buf, sizeof(buf), "R:%lu", fanRPM);
    SSD1306_GotoXY(82, 51);
    SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
}

static void Display_UpdateTemperatures(void)
{
    char buf[32];
    int  ti, tf;

    SSD1306_GotoXY(20, 0);
    SSD1306_Puts("TEMPERATURES", &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_DrawLine(0, 12, 127, 12, SSD1306_COLOR_WHITE);

#define TEMP_STR(b, lbl, T)                                               \
    do {                                                                   \
        if ((T) <= 0.0f) { snprintf((b), sizeof(b), "%s ---", (lbl)); }   \
        else {                                                             \
            ti = (int)(T);                                                 \
            tf = (int)fabsf((T) * 10.0f) % 10;                            \
            snprintf((b), sizeof(b), "%s %d.%d C", (lbl), ti, tf);        \
        }                                                                  \
    } while (0)

    TEMP_STR(buf, "Temp 1:", temp1_C);
    SSD1306_GotoXY(4, 18); SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);

    TEMP_STR(buf, "Temp 2:", temp2_C);
    SSD1306_GotoXY(4, 32); SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);

    TEMP_STR(buf, "Fan   :", fanTemp_C);
    SSD1306_GotoXY(4, 46); SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);

#undef TEMP_STR

    snprintf(buf, sizeof(buf), "Speed : %lu RPM", fanRPM);
    SSD1306_GotoXY(4, 56);
    SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
}

static void Display_UpdateFanControl(void)
{
    char buf[32];

    SSD1306_GotoXY(16, 0);
    SSD1306_Puts("FAN CONTROL", &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_DrawLine(0, 12, 127, 12, SSD1306_COLOR_WHITE);

    Display_DrawRow(16U, FAN_MENU_LABELS[0],
                    (fanCtrl.menuIndex == 0 && fanCtrl.mode == FAN_MODE_AUTO));

    if      (fanCtrl.editingPercent)          { snprintf(buf, sizeof(buf), "Manual [%u%%]", fanCtrl.manualPercent); }
    else if (fanCtrl.mode == FAN_MODE_MANUAL) { snprintf(buf, sizeof(buf), "Manual  %u%%",  fanCtrl.manualPercent); }
    else                                      { snprintf(buf, sizeof(buf), "Manual"); }
    Display_DrawRow(28U, buf, fanCtrl.menuIndex == 1);

    Display_DrawRow(40U, FAN_MENU_LABELS[2], fanCtrl.menuIndex == 2);

    const char *ms = (fanCtrl.mode == FAN_MODE_AUTO) ? "AUTO" : "MAN";
    snprintf(buf, sizeof(buf), "Mode:%-4s RPM:%lu", ms, fanRPM);
    SSD1306_GotoXY(4, 54);
    SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
}

static void Display_UpdateCalMenu(void)
{
    char buf[32];

    SSD1306_GotoXY(16, 0);
    SSD1306_Puts("CALIBRATION", &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_DrawLine(0, 12, 127, 12, SSD1306_COLOR_WHITE);

    uint8_t startIdx = (calScreen.menuIndex > 3) ? (uint8_t)(calScreen.menuIndex - 3) : 0U;

    for (uint8_t i = 0U; i < 4U; i++)
    {
        uint8_t idx = startIdx + i;
        if (idx < (uint8_t)CAL_ITEM_COUNT)
        {
            Display_DrawRow(14U + (i * 12U), CAL_MENU_LABELS[idx],
                            calScreen.menuIndex == (int8_t)idx);
        }
    }

    float       showScale  = 0.0f;
    const char *scaleLabel = NULL;

    switch (calScreen.menuIndex)
    {
        case CAL_ITEM_V1_TRUE: case CAL_ITEM_V1_RES:
            showScale = cal.volt1Scale; scaleLabel = "V1 Scl:"; break;
        case CAL_ITEM_V2_TRUE: case CAL_ITEM_V2_RES:
            showScale = cal.volt2Scale; scaleLabel = "V2 Scl:"; break;
        case CAL_ITEM_I_TRUE:  case CAL_ITEM_I_RES:
            showScale = cal.currScale;  scaleLabel = "I  Scl:"; break;
        default: break;
    }

    if (scaleLabel != NULL)
    {
        uint32_t sInt  = (uint32_t)showScale;
        uint32_t sFrac = (uint32_t)(showScale * 1000.0f + 0.5f) % 1000U;
        snprintf(buf, sizeof(buf), "%s%lu.%03lu", scaleLabel, sInt, sFrac);
        SSD1306_GotoXY(0, 54);
        SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
    }
}

static void Display_UpdateCalEntry(void)
{
    char buf[32];

    SSD1306_GotoXY(4, 0);
    switch (calEntry.type)
    {
        case CAL_ENTRY_V1_TRUE:  SSD1306_Puts("CAL V1 TRUE VAL", &Font_7x10, SSD1306_COLOR_WHITE); break;
        case CAL_ENTRY_V2_TRUE:  SSD1306_Puts("CAL V2 TRUE VAL", &Font_7x10, SSD1306_COLOR_WHITE); break;
        case CAL_ENTRY_I_TRUE:   SSD1306_Puts("CAL I  TRUE VAL", &Font_7x10, SSD1306_COLOR_WHITE); break;
        case CAL_ENTRY_V1_RTOP:  SSD1306_Puts("V1: R_top (kO)",  &Font_7x10, SSD1306_COLOR_WHITE); break;
        case CAL_ENTRY_V1_RBOT:  SSD1306_Puts("V1: R_bot (kO)",  &Font_7x10, SSD1306_COLOR_WHITE); break;
        case CAL_ENTRY_V2_RTOP:  SSD1306_Puts("V2: R_top (kO)",  &Font_7x10, SSD1306_COLOR_WHITE); break;
        case CAL_ENTRY_V2_RBOT:  SSD1306_Puts("V2: R_bot (kO)",  &Font_7x10, SSD1306_COLOR_WHITE); break;
        case CAL_ENTRY_I_SHUNT:  SSD1306_Puts("I: Shunt (mO)",   &Font_7x10, SSD1306_COLOR_WHITE); break;
        case CAL_ENTRY_I_GAIN:   SSD1306_Puts("I: Amp Gain",     &Font_7x10, SSD1306_COLOR_WHITE); break;
        default: break;
    }

    SSD1306_DrawLine(0, 12, 127, 12, SSD1306_COLOR_WHITE);

    SSD1306_GotoXY(4, 14);
    switch (calEntry.type)
    {
        case CAL_ENTRY_V1_TRUE:
            snprintf(buf, sizeof(buf), "Now:%lu.%03luV",
                     lastVolt1_mV / 1000UL, lastVolt1_mV % 1000UL);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            break;

        case CAL_ENTRY_V2_TRUE:
            snprintf(buf, sizeof(buf), "Now:%lu.%03luV",
                     lastVolt2_mV / 1000UL, lastVolt2_mV % 1000UL);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            break;

        case CAL_ENTRY_I_TRUE:
            snprintf(buf, sizeof(buf), "Now:%lu.%03luA",
                     meas.current_mA / 1000UL, meas.current_mA % 1000UL);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            break;

        case CAL_ENTRY_V1_RBOT:
        case CAL_ENTRY_V2_RBOT:
            snprintf(buf, sizeof(buf), "R_top:%lu.%lukO",
                     calEntry.tempVal / 10UL, calEntry.tempVal % 10UL);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            break;

        case CAL_ENTRY_I_GAIN:
            snprintf(buf, sizeof(buf), "Shunt:%lumO", calEntry.tempVal);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            break;

        default: break;
    }

    Display_DrawCalEditor();

    SSD1306_GotoXY(0, 55);
    SSD1306_Puts("L/R=cursor  Btn=OK", &Font_7x10, SSD1306_COLOR_WHITE);
}

static void Display_UpdateWifi(void)
{
    char buf[32];

    SSD1306_GotoXY(40, 0);
    SSD1306_Puts("WiFi", &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_DrawLine(0, 12, 127, 12, SSD1306_COLOR_WHITE);

    snprintf(buf, sizeof(buf), "IP:%s", wifiScreen.ipAddr);
    SSD1306_GotoXY(4, 14);
    SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);

    Display_DrawRow(28U, WIFI_MENU_LABELS[WIFI_ITEM_RESET_DATA],
                    wifiScreen.menuIndex == (int8_t)WIFI_ITEM_RESET_DATA);
    Display_DrawRow(40U, WIFI_MENU_LABELS[WIFI_ITEM_RESET_WIFI],
                    wifiScreen.menuIndex == (int8_t)WIFI_ITEM_RESET_WIFI);
    Display_DrawRow(52U, WIFI_MENU_LABELS[WIFI_ITEM_BACK],
                    wifiScreen.menuIndex == (int8_t)WIFI_ITEM_BACK);
}

static void Display_Refresh(void)
{
    SSD1306_Fill(SSD1306_COLOR_BLACK);

    switch (app.screen)
    {
        case SCREEN_MODE_SELECT:  Display_UpdateMainMenu();     break;
        case SCREEN_SUBMENU:      Display_UpdateSubmenu();      break;
        case SCREEN_SET_VALUE:    Display_UpdateSetValue();     break;
        case SCREEN_RUNNING:      Display_UpdateRunning();      break;
        case SCREEN_TEMPERATURES: Display_UpdateTemperatures(); break;
        case SCREEN_FAN_CONTROL:  Display_UpdateFanControl();   break;
        case SCREEN_CALIBRATION:  Display_UpdateCalMenu();      break;
        case SCREEN_CAL_ENTRY:    Display_UpdateCalEntry();     break;
        case SCREEN_WIFI:         Display_UpdateWifi();         break;
        default: break;
    }

    SSD1306_UpdateScreen();
}

/* ---- Sensor services ------------------------------------------------------ */

static void Sensor_ServiceTemperatures(void)
{
    static uint32_t t = 0U;
    if ((HAL_GetTick() - t) < TEMP_READ_INTERVAL_MS) { return; }
    t = HAL_GetTick();

    temp1_C   = NTC_ToTemperatureCelsius(ADC_ReadChannel(&hadc1, ADC_CHANNEL_2));
    temp2_C   = NTC_ToTemperatureCelsius(ADC_ReadChannel(&hadc1, ADC_CHANNEL_3));
    fanTemp_C = NTC_ToTemperatureCelsius(ADC_ReadChannel(&hadc2, ADC_CHANNEL_4));
}

static void Sensor_ServiceADS1115(void)
{
    static uint32_t lastPoll      = 0U;
    static uint8_t  state         = 0U;
    static uint8_t  activeVoltCh  = ADS1115_CH_VOLT2;
    static uint8_t  volt2BadCount = 0U;
    static uint8_t  volt1Retries  = 0U;

    if ((HAL_GetTick() - lastPoll) < ADS_POLL_INTERVAL_MS) { return; }
    lastPoll = HAL_GetTick();

    if (state == 0U)
    {
        float rawVolt = ADS1115_ReadConversion(&hi2c1);

        if (activeVoltCh == ADS1115_CH_VOLT2)
        {
            lastRawVolt2 = rawVolt;
            float    trueVolt = rawVolt * cal.volt2Scale
                                + (float)cal.volt2Offset_mV / 1000.0f;

            if (trueVolt > 0.5f)
            {
                uint32_t v    = (uint32_t)(trueVolt * 1000.0f);
                meas.voltage_mV = v;
                lastVolt2_mV    = v;
                volt2BadCount   = 0U;
            }
            else
            {
                if (++volt2BadCount >= VOLT2_BAD_THRESH)
                {
                    activeVoltCh  = ADS1115_CH_VOLT;
                    volt2BadCount = 0U;
                    volt1Retries  = 0U;
                }
            }
        }
        else
        {
            lastRawVolt1 = rawVolt;
            float    trueVolt = rawVolt * cal.volt1Scale
                                + (float)cal.volt1Offset_mV / 1000.0f;
            uint32_t v    = (uint32_t)(trueVolt * 1000.0f);
            meas.voltage_mV = v;
            lastVolt1_mV    = v;

            if (++volt1Retries > 100U)
            {
                activeVoltCh = ADS1115_CH_VOLT2;
                volt1Retries = 0U;
            }
        }

        ADS1115_StartConversion(&hi2c1, ADS1115_CH_CURRENT);
        state = 1U;
    }
    else
    {
        float rawCurr = ADS1115_ReadConversion(&hi2c1);
        lastRawCurr   = rawCurr;

        int32_t current_mA = (int32_t)(rawCurr * cal.currScale * 1000.0f)
                             + cal.currOffset_mA;
        meas.current_mA = (current_mA > 0) ? (uint32_t)current_mA : 0U;

        meas.power_mW = (uint32_t)((float)meas.voltage_mV * (float)meas.current_mA
                                   / 1000.0f);

        ADS1115_StartConversion(&hi2c1, activeVoltCh);
        state = 0U;
    }
}

static void Sensor_ServiceFanRPM(void)
{
    static uint32_t t = 0U;
    if ((HAL_GetTick() - t) < FAN_RPM_WINDOW_MS) { return; }
    t = HAL_GetTick();

    uint32_t pulses = __HAL_TIM_GET_COUNTER(&htim1);
    __HAL_TIM_SET_COUNTER(&htim1, 0);
    fanRPM = pulses * (60UL / FAN_PULSES_PER_REV);
}

/* ---- Thermal management --------------------------------------------------- */

static void Thermal_ServiceFan(void)
{
    static uint32_t t = 0U;
    if ((HAL_GetTick() - t) < FAN_PWM_UPDATE_MS) { return; }
    t = HAL_GetTick();

    uint32_t pwm;

    if (fanCtrl.mode == FAN_MODE_MANUAL)
    {
        if (fanCtrl.manualPercent == 0U)
        {
            pwm = 0U;
        }
        else
        {
            pwm = (uint32_t)((fanCtrl.manualPercent * FAN_PWM_MAX) / 100UL);
            if (pwm < FAN_PWM_MIN) { pwm = FAN_PWM_MIN; }
        }
    }
    else
    {
        if      (fanTemp_C < 0.0f)            { pwm = FAN_PWM_MAX; }
        else if (fanTemp_C <= FAN_TEMP_MIN_C) { pwm = 0U; }
        else if (fanTemp_C >= FAN_TEMP_MAX_C) { pwm = FAN_PWM_MAX; }
        else
        {
            float ratio = (fanTemp_C - FAN_TEMP_MIN_C) / (FAN_TEMP_MAX_C - FAN_TEMP_MIN_C);
            pwm = FAN_PWM_MIN + (uint32_t)(ratio * (float)(FAN_PWM_MAX - FAN_PWM_MIN));
        }
    }

    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, pwm);
}

static void Thermal_ServiceFanAlarm(void)
{
    static uint32_t lastAlarm  = 0U;
    static uint8_t  phase      = 0U;
    static uint32_t phaseTimer = 0U;

    if (fanRPM > 0U || fanTemp_C <= FAN_FAULT_TEMP_THRESH_C ||
        fanCtrl.mode == FAN_MODE_MANUAL)
    {
        phase = 0U;
        return;
    }

    uint32_t now = HAL_GetTick();

    if (phase == 1U && (now - phaseTimer) >= 200UL)
    {
        Buzzer_Trigger();
        phase     = 0U;
        lastAlarm = now;
        return;
    }

    if (phase == 0U && (now - lastAlarm) >= FAN_FAULT_ALARM_MS)
    {
        Buzzer_Trigger();
        phaseTimer = now;
        phase      = 1U;
    }
}

/* ---- Load control --------------------------------------------------------- */

static void Load_CheckBattCutoff(void)
{
    uint8_t vCut = (meas.voltage_mV <= batt.cutoffVoltage_mV && meas.voltage_mV > 500UL);
    uint8_t cCut = (meas.capacity_mAh >= BATT_MAX_CAPACITY_MAH);

    if (vCut || cCut)
    {
        batt.finished = 1U;
        Buzzer_Trigger();
        NVM_Save();
    }
}

static void Load_ServiceCapacity(void)
{
    static uint32_t mahTimer = 0U;
    static uint32_t nvmTimer = 0U;

    if (app.screen != SCREEN_RUNNING)
    {
        mahTimer = HAL_GetTick();
        nvmTimer = HAL_GetTick();
        return;
    }

    if (batt.finished) { return; }

    if (app.loadMode == MODE_BATT_TEST) { Load_CheckBattCutoff(); }

    if ((HAL_GetTick() - mahTimer) >= MAH_UPDATE_INTERVAL_MS)
    {
        mahTimer              = HAL_GetTick();
        batt.mAh_accumulator += (float)meas.current_mA / 3600.0f;
        meas.capacity_mAh     = (uint32_t)batt.mAh_accumulator;
    }

    if (app.loadMode == MODE_BATT_TEST &&
        (HAL_GetTick() - nvmTimer) >= NVM_SAVE_INTERVAL_MS)
    {
        nvmTimer = HAL_GetTick();
        NVM_Save();
    }
}

static void UART_SendData(void)
{
    if (app.screen != SCREEN_RUNNING) { return; }

    static uint32_t lastTx = 0U;
    if ((HAL_GetTick() - lastTx) < UART_TX_INTERVAL_MS) { return; }
    lastTx = HAL_GetTick();

    float voltage  = (float)meas.voltage_mV / 1000.0f;
    float current  = (float)meas.current_mA / 1000.0f;
    float fan_temp = (fanTemp_C > -50.0f) ? fanTemp_C : 0.0f;
    float t1       = (temp1_C   > -50.0f) ? temp1_C   : 0.0f;
    float t2       = (temp2_C   > -50.0f) ? temp2_C   : 0.0f;

    char buf[64] = { 0 };
    char tmp[16];

    FloatToString(tmp, voltage,  2); strcpy(buf, tmp); strcat(buf, ",");
    FloatToString(tmp, current,  2); strcat(buf, tmp); strcat(buf, ",");
    FloatToString(tmp, fan_temp, 1); strcat(buf, tmp); strcat(buf, ",");
    FloatToString(tmp, t1,       1); strcat(buf, tmp); strcat(buf, ",");
    FloatToString(tmp, t2,       1); strcat(buf, tmp); strcat(buf, "\n");

    HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)strlen(buf), 100);
}

static void UART_ParseRxLine(const char *line)
{
    if (strncmp(line, "IP:", 3) != 0) { return; }

    strncpy(wifiScreen.ipAddr, line + 3, WIFI_IP_BUF_LEN - 1U);
    wifiScreen.ipAddr[WIFI_IP_BUF_LEN - 1U] = '\0';

    if (app.screen == SCREEN_WIFI) { app.displayNeedsUpdate = 1; }
}

static uint16_t Load_ComputeDAC(void)
{
    if (app.screen != SCREEN_RUNNING || batt.finished) { return 0U; }

    uint32_t target_mA = 0U;

    switch (app.loadMode)
    {
        case MODE_CC:
            editor.value = DigitEditor_BuildValue();
            target_mA    = editor.value;
            break;

        case MODE_CP:
        {
            editor.value     = DigitEditor_BuildValue();
            uint32_t volt_mV = meas.voltage_mV;
            if (volt_mV < 100UL) { return 0U; }
            target_mA = (editor.value * 1000UL) / volt_mV;
            break;
        }

        case MODE_BATT_TEST:
            target_mA = batt.dischargeCurrent_mA;
            break;

        default: return 0U;
    }

    if (target_mA > MAX_CURRENT_MA) { target_mA = MAX_CURRENT_MA; }

    uint32_t dac = (target_mA * DAC_FULL_SCALE) / VDD_MV;
    if (dac > DAC_FULL_SCALE) { dac = DAC_FULL_SCALE; }

    return (uint16_t)dac;
}

static void Load_UpdateDAC(void)
{
    MCP4725_SetVoltage(&hi2c2, Load_ComputeDAC());
}

static void Encoder_Service(void)
{
    int16_t cur   = (int16_t)__HAL_TIM_GET_COUNTER(&htim2);
    int16_t delta = cur - lastEncoderRaw;
    lastEncoderRaw = cur;

    if (delta == 0) { return; }

    static int16_t acc = 0;
    acc += delta;

    int8_t detents = (int8_t)(acc / ENCODER_DIVIDER);
    if (detents == 0) { return; }

    acc -= detents * ENCODER_DIVIDER;

    int8_t dir = (detents > 0) ? 1 : -1;
    Buzzer_Trigger();

    if (app.screen == SCREEN_FAN_CONTROL && fanCtrl.editingPercent)
    {
        int16_t p = (int16_t)fanCtrl.manualPercent + detents;
        if (p <   0) { p =   0; }
        if (p > 100) { p = 100; }
        fanCtrl.manualPercent  = (uint8_t)p;
        app.displayNeedsUpdate = 1;
        return;
    }

    switch (app.screen)
    {
        case SCREEN_MODE_SELECT:
            app.menuIndex += dir;
            if (app.menuIndex < 0)                              { app.menuIndex = (int8_t)(MAIN_MENU_ITEM_COUNT - 1U); }
            if (app.menuIndex >= (int8_t)MAIN_MENU_ITEM_COUNT)  { app.menuIndex = 0; }
            break;

        case SCREEN_SUBMENU:
            app.menuIndex += dir;
            if (app.menuIndex < 0)                       { app.menuIndex = (int8_t)(SUBMENU_COUNT - 1U); }
            if (app.menuIndex >= (int8_t)SUBMENU_COUNT)  { app.menuIndex = 0; }
            break;

        case SCREEN_SET_VALUE:
            editor.digits[editor.cursor] += dir;
            DigitEditor_Clamp(editor.cursor);
            break;

        case SCREEN_FAN_CONTROL:
            fanCtrl.menuIndex += dir;
            if (fanCtrl.menuIndex < 0)                        { fanCtrl.menuIndex = (int8_t)(FAN_MENU_COUNT - 1U); }
            if (fanCtrl.menuIndex >= (int8_t)FAN_MENU_COUNT)  { fanCtrl.menuIndex = 0; }
            break;

        case SCREEN_CALIBRATION:
            calScreen.menuIndex += dir;
            if (calScreen.menuIndex < 0)                         { calScreen.menuIndex = (int8_t)(CAL_ITEM_COUNT - 1U); }
            if (calScreen.menuIndex >= (int8_t)CAL_ITEM_COUNT)   { calScreen.menuIndex = 0; }
            break;

        case SCREEN_CAL_ENTRY:
            calEntry.digits[calEntry.cursor] += dir;
            if (calEntry.digits[calEntry.cursor] > 9) { calEntry.digits[calEntry.cursor] = 0; }
            if (calEntry.digits[calEntry.cursor] < 0) { calEntry.digits[calEntry.cursor] = 9; }
            break;

        case SCREEN_WIFI:
            wifiScreen.menuIndex += dir;
            if (wifiScreen.menuIndex < 0)                         { wifiScreen.menuIndex = (int8_t)(WIFI_ITEM_COUNT - 1U); }
            if (wifiScreen.menuIndex >= (int8_t)WIFI_ITEM_COUNT)  { wifiScreen.menuIndex = 0; }
            break;

        default: break;
    }

    app.displayNeedsUpdate = 1;
}
/* USER CODE END 0 */

/* =============================================================================
 * main()
 * ============================================================================= */
int main(void)
{
    /* ----- System initialisation ------------------------------------------ */
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_I2C1_Init();
    MX_I2C2_Init();
    MX_TIM1_Init();
    MX_TIM2_Init();
    MX_TIM3_Init();
    MX_TIM4_Init();
    MX_ADC1_Init();
    MX_ADC2_Init();
    MX_USART1_UART_Init();
    /* USER CODE BEGIN 2 */
    HAL_UART_Receive_IT(&huart1, &rxByte, 1);
    ESP_HardwareReset();

    HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    lastEncoderRaw = 0;

    HAL_TIM_Base_Start(&htim1);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);

    NVM_Load();

    HAL_Delay(100);
    SSD1306_Init();
    SSD1306_Fill(SSD1306_COLOR_BLACK);
    SSD1306_UpdateScreen();
    Display_Refresh();

    HAL_UART_Receive_IT(&huart1, &rxByte, 1);
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN 3 */
        Sensor_ServiceTemperatures();
        Sensor_ServiceADS1115();
        Sensor_ServiceFanRPM();
        Thermal_ServiceFan();
        Thermal_ServiceFanAlarm();
        Load_ServiceCapacity();
        Buzzer_Service();
        Encoder_Service();
        Load_UpdateDAC();
        UART_SendData();

        static uint32_t refreshTimer = 0;
        if ((app.screen == SCREEN_RUNNING || app.screen == SCREEN_TEMPERATURES) &&
            (HAL_GetTick() - refreshTimer) >= DISPLAY_REFRESH_MS)
        {
            refreshTimer           = HAL_GetTick();
            app.displayNeedsUpdate = 1;
        }

        if (app.displayNeedsUpdate)
        {
            app.displayNeedsUpdate = 0;
            Display_Refresh();
        }

        if (app.screen == SCREEN_WIFI)
        {
            static uint32_t lastIpReq = 0;
            if ((HAL_GetTick() - lastIpReq) > 2000UL)
            {
                lastIpReq = HAL_GetTick();
                const char *req = "CMD:GET_IP\n";
                HAL_UART_Transmit(&huart1, (uint8_t *)req, (uint16_t)strlen(req), 50);
            }
        }
        /* USER CODE END 3 */
    }
}

/* =============================================================================
 * Clock and peripheral initialisation — generated by CubeMX
 * Only USER CODE sections inside these functions survive regeneration.
 * ============================================================================= */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef       osc = { 0 };
    RCC_ClkInitTypeDef       clk = { 0 };
    RCC_PeriphCLKInitTypeDef pck = { 0 };

    osc.OscillatorType  = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState        = RCC_HSE_ON;
    osc.HSEPredivValue  = RCC_HSE_PREDIV_DIV1;
    osc.HSIState        = RCC_HSI_ON;
    osc.PLL.PLLState    = RCC_PLL_ON;
    osc.PLL.PLLSource   = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL      = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) { Error_Handler(); }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK   | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }

    pck.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    pck.AdcClockSelection    = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&pck) != HAL_OK) { Error_Handler(); }
}

static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef s = { 0 };

    hadc1.Instance                   = ADC1;
    hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) { Error_Handler(); }

    s.Channel      = ADC_CHANNEL_2;
    s.Rank         = ADC_REGULAR_RANK_1;
    s.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &s) != HAL_OK) { Error_Handler(); }
}

static void MX_ADC2_Init(void)
{
    ADC_ChannelConfTypeDef s = { 0 };

    hadc2.Instance                   = ADC2;
    hadc2.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    hadc2.Init.ContinuousConvMode    = DISABLE;
    hadc2.Init.DiscontinuousConvMode = DISABLE;
    hadc2.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc2.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc2.Init.NbrOfConversion       = 1;
    if (HAL_ADC_Init(&hadc2) != HAL_OK) { Error_Handler(); }

    s.Channel      = ADC_CHANNEL_4;
    s.Rank         = ADC_REGULAR_RANK_1;
    s.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
    if (HAL_ADC_ConfigChannel(&hadc2, &s) != HAL_OK) { Error_Handler(); }
}

static void MX_I2C1_Init(void)
{
    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 400000;
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2     = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) { Error_Handler(); }
}

static void MX_I2C2_Init(void)
{
    hi2c2.Instance             = I2C2;
    hi2c2.Init.ClockSpeed      = 100000;
    hi2c2.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c2.Init.OwnAddress1     = 0;
    hi2c2.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2     = 0;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c2) != HAL_OK) { Error_Handler(); }
}

static void MX_TIM1_Init(void)
{
    TIM_ClockConfigTypeDef  sc = { 0 };
    TIM_MasterConfigTypeDef mc = { 0 };
    TIM_IC_InitTypeDef      ic = { 0 };

    htim1.Instance               = TIM1;
    htim1.Init.Prescaler         = 0;
    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim1.Init.Period            = 65535;
    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim1) != HAL_OK) { Error_Handler(); }

    sc.ClockSource    = TIM_CLOCKSOURCE_TI1;
    sc.ClockPolarity  = TIM_CLOCKPOLARITY_RISING;
    sc.ClockPrescaler = TIM_CLOCKPRESCALER_DIV1;
    sc.ClockFilter    = 0x08;
    if (HAL_TIM_ConfigClockSource(&htim1, &sc) != HAL_OK) { Error_Handler(); }

    mc.MasterOutputTrigger = TIM_TRGO_RESET;
    mc.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &mc) != HAL_OK) { Error_Handler(); }

    ic.ICPolarity  = TIM_INPUTCHANNELPOLARITY_RISING;
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV1;
    ic.ICFilter    = 8;
    if (HAL_TIM_IC_ConfigChannel(&htim1, &ic, TIM_CHANNEL_1) != HAL_OK) { Error_Handler(); }
}

static void MX_TIM2_Init(void)
{
    TIM_Encoder_InitTypeDef ec = { 0 };
    TIM_MasterConfigTypeDef mc = { 0 };

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 0;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 65535;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    ec.EncoderMode  = TIM_ENCODERMODE_TI1;
    ec.IC1Polarity  = TIM_ICPOLARITY_RISING;
    ec.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    ec.IC1Prescaler = TIM_ICPSC_DIV1;
    ec.IC1Filter    = 0;
    ec.IC2Polarity  = TIM_ICPOLARITY_RISING;
    ec.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    ec.IC2Prescaler = TIM_ICPSC_DIV1;
    ec.IC2Filter    = 0;
    if (HAL_TIM_Encoder_Init(&htim2, &ec) != HAL_OK) { Error_Handler(); }

    mc.MasterOutputTrigger = TIM_TRGO_RESET;
    mc.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &mc) != HAL_OK) { Error_Handler(); }
}

static void MX_TIM3_Init(void)
{
    TIM_ClockConfigTypeDef  sc = { 0 };
    TIM_MasterConfigTypeDef mc = { 0 };

    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 7199;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 65535;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&htim3) != HAL_OK) { Error_Handler(); }

    sc.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim3, &sc) != HAL_OK) { Error_Handler(); }

    mc.MasterOutputTrigger = TIM_TRGO_RESET;
    mc.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &mc) != HAL_OK) { Error_Handler(); }
}

static void MX_TIM4_Init(void)
{
    TIM_ClockConfigTypeDef  sc = { 0 };
    TIM_MasterConfigTypeDef mc = { 0 };
    TIM_OC_InitTypeDef      oc = { 0 };

    htim4.Instance               = TIM4;
    htim4.Init.Prescaler         = 0;
    htim4.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim4.Init.Period            = 2879;
    htim4.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim4) != HAL_OK) { Error_Handler(); }

    sc.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim4, &sc) != HAL_OK) { Error_Handler(); }
    if (HAL_TIM_PWM_Init(&htim4)              != HAL_OK) { Error_Handler(); }

    mc.MasterOutputTrigger = TIM_TRGO_RESET;
    mc.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &mc) != HAL_OK) { Error_Handler(); }

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim4, &oc, TIM_CHANNEL_3) != HAL_OK) { Error_Handler(); }

    HAL_TIM_MspPostInit(&htim4);
}

static void MX_USART1_UART_Init(void)
{
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) { Error_Handler(); }
}

static void MX_DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = { 0 };

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOB, BUZZER_Pin,    GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, ESP_RESET_Pin, GPIO_PIN_SET);

    g.Pin  = R_BTN_Pin | BTN2_Pin | BTN1_Pin;
    g.Mode = GPIO_MODE_IT_RISING;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);

    g.Pin  = ALRT_Pin;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ALRT_GPIO_Port, &g);

    g.Pin   = ESP_RESET_Pin | BUZZER_Pin;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);

    HAL_NVIC_SetPriority(EXTI0_IRQn,     2, 0); HAL_NVIC_EnableIRQ(EXTI0_IRQn);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 2, 0); HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) { return; }

    char c = (char)rxByte;

    if (c == '\n' || c == '\r')
    {
        if (rxLineIdx > 0U)
        {
            rxLineBuf[rxLineIdx] = '\0';
            UART_ParseRxLine(rxLineBuf);
            rxLineIdx = 0U;
        }
    }
    else
    {
        if (rxLineIdx < (UART_RX_BUF_LEN - 1U))
        {
            rxLineBuf[rxLineIdx++] = c;
        }
        else
        {
            rxLineIdx = 0U;
        }
    }

    HAL_UART_Receive_IT(&huart1, &rxByte, 1);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        HAL_UART_AbortReceive(huart);
        rxLineIdx = 0U;
        memset(rxLineBuf, 0, UART_RX_BUF_LEN);
        HAL_UART_Receive_IT(&huart1, &rxByte, 1);
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    static uint32_t lastTick = 0U;
    uint32_t        now      = HAL_GetTick();

    if ((now - lastTick) < DEBOUNCE_MS) { return; }
    lastTick = now;

    Buzzer_Trigger();

    if (GPIO_Pin == GPIO_PIN_15)
    {
        if      (app.screen == SCREEN_SET_VALUE) { DigitEditor_MoveCursor(-1); }
        else if (app.screen == SCREEN_CAL_ENTRY) { CalEntry_MoveCursor(-1); }
        app.displayNeedsUpdate = 1;
        return;
    }

    if (GPIO_Pin == GPIO_PIN_14)
    {
        if      (app.screen == SCREEN_SET_VALUE) { DigitEditor_MoveCursor(+1); }
        else if (app.screen == SCREEN_CAL_ENTRY) { CalEntry_MoveCursor(+1); }
        app.displayNeedsUpdate = 1;
        return;
    }

    if (GPIO_Pin != GPIO_PIN_0) { return; }

    switch (app.screen)
    {
        case SCREEN_MODE_SELECT:
            savedMainMenuIndex = app.menuIndex;

            if      (app.menuIndex == 3) { app.screen = SCREEN_TEMPERATURES;                          }
            else if (app.menuIndex == 4) { app.screen = SCREEN_FAN_CONTROL;  fanCtrl.menuIndex  = 0;  }
            else if (app.menuIndex == 5) { app.screen = SCREEN_CALIBRATION;  calScreen.menuIndex = 0; }
            else if (app.menuIndex == 6) { app.screen = SCREEN_WIFI;         wifiScreen.menuIndex = 0;}
            else
            {
                app.loadMode = (LoadMode_t)app.menuIndex;
                app.screen   = SCREEN_SUBMENU;
            }
            app.menuIndex = 0;
            break;

        case SCREEN_SUBMENU:
            switch ((SubMenuItem_t)app.menuIndex)
            {
                case SUBMENU_SET:
                    app.screen    = SCREEN_SET_VALUE;
                    editor.cursor = 0;
                    if (app.loadMode == MODE_BATT_TEST)
                    {
                        batt.mAh_accumulator = 0.0f;
                        meas.capacity_mAh    = 0U;
                        batt.finished        = 0U;
                        batt.editField       = BATT_EDIT_CURRENT;
                        DigitEditor_LoadBattField(BATT_EDIT_CURRENT);
                    }
                    break;

                case SUBMENU_START:
                    batt.finished = 0U;
                    app.screen    = SCREEN_RUNNING;
                    break;

                case SUBMENU_BACK:
                    app.screen    = SCREEN_MODE_SELECT;
                    app.menuIndex = savedMainMenuIndex;
                    break;

                default: break;
            }
            break;

        case SCREEN_SET_VALUE:
            if (app.loadMode == MODE_BATT_TEST)
            {
                if (batt.editField == BATT_EDIT_CURRENT)
                {
                    DigitEditor_SaveBattField(BATT_EDIT_CURRENT);
                    batt.editField = BATT_EDIT_VOLTAGE;
                    DigitEditor_LoadBattField(BATT_EDIT_VOLTAGE);
                }
                else
                {
                    DigitEditor_SaveBattField(BATT_EDIT_VOLTAGE);
                    batt.editField = BATT_EDIT_CURRENT;
                    app.screen     = SCREEN_SUBMENU;
                    app.menuIndex  = 0;
                }
            }
            else
            {
                app.screen    = SCREEN_SUBMENU;
                app.menuIndex = 0;
            }
            break;

        case SCREEN_RUNNING:
            app.screen    = SCREEN_SUBMENU;
            app.menuIndex = 0;
            break;

        case SCREEN_TEMPERATURES:
            app.screen    = SCREEN_MODE_SELECT;
            app.menuIndex = savedMainMenuIndex;
            break;

        case SCREEN_FAN_CONTROL:
            switch (fanCtrl.menuIndex)
            {
                case 0:
                    fanCtrl.mode           = FAN_MODE_AUTO;
                    fanCtrl.editingPercent = 0U;
                    break;

                case 1:
                    fanCtrl.mode           = FAN_MODE_MANUAL;
                    fanCtrl.editingPercent ^= 1U;
                    break;

                case 2:
                    fanCtrl.editingPercent = 0U;
                    app.screen             = SCREEN_MODE_SELECT;
                    app.menuIndex          = savedMainMenuIndex;
                    break;

                default: break;
            }
            break;

        case SCREEN_CALIBRATION:
            switch ((CalMenuItem_t)calScreen.menuIndex)
            {
                case CAL_ITEM_V1_TRUE: CalEntry_Setup(CAL_ENTRY_V1_TRUE, 0); app.screen = SCREEN_CAL_ENTRY; break;
                case CAL_ITEM_V2_TRUE: CalEntry_Setup(CAL_ENTRY_V2_TRUE, 0); app.screen = SCREEN_CAL_ENTRY; break;
                case CAL_ITEM_I_TRUE:  CalEntry_Setup(CAL_ENTRY_I_TRUE,  0); app.screen = SCREEN_CAL_ENTRY; break;
                case CAL_ITEM_V1_RES:  CalEntry_Setup(CAL_ENTRY_V1_RTOP, 0); app.screen = SCREEN_CAL_ENTRY; break;
                case CAL_ITEM_V2_RES:  CalEntry_Setup(CAL_ENTRY_V2_RTOP, 0); app.screen = SCREEN_CAL_ENTRY; break;
                case CAL_ITEM_I_RES:   CalEntry_Setup(CAL_ENTRY_I_SHUNT, 0); app.screen = SCREEN_CAL_ENTRY; break;

                case CAL_ITEM_SAVE:
                    NVM_Save();
                    break;

                case CAL_ITEM_RESET:
                    cal.volt1Scale     = VOLT1_CAL_DEFAULT;
                    cal.volt1Offset_mV = 0;
                    cal.volt2Scale     = VOLT2_CAL_DEFAULT;
                    cal.volt2Offset_mV = 0;
                    cal.currScale      = CURR_CAL_DEFAULT;
                    cal.currOffset_mA  = 0;
                    break;

                case CAL_ITEM_BACK:
                    app.screen    = SCREEN_MODE_SELECT;
                    app.menuIndex = savedMainMenuIndex;
                    break;

                default: break;
            }
            break;

        case SCREEN_CAL_ENTRY:
            CalEntry_Confirm();
            break;

        case SCREEN_WIFI:
            switch ((WifiMenuItem_t)wifiScreen.menuIndex)
            {
                case WIFI_ITEM_RESET_DATA:
                    break;

                case WIFI_ITEM_RESET_WIFI:
                {
                    const char *cmd = "CMD:RESET_WIFI\n";
                    HAL_UART_Transmit(&huart1, (uint8_t *)cmd, (uint16_t)strlen(cmd), 50);
                    strncpy(wifiScreen.ipAddr, "Resetting...", WIFI_IP_BUF_LEN - 1U);
                    wifiScreen.ipAddr[WIFI_IP_BUF_LEN - 1U] = '\0';
                    app.displayNeedsUpdate = 1;
                    break;
                }

                case WIFI_ITEM_BACK:
                    app.screen    = SCREEN_MODE_SELECT;
                    app.menuIndex = savedMainMenuIndex;
                    break;

                default: break;
            }
            break;

        default: break;
    }

    __HAL_TIM_SET_COUNTER(&htim2, 0);
    lastEncoderRaw         = 0;
    app.displayNeedsUpdate = 1;
}

void ESP_HardwareReset(void)
{
    HAL_GPIO_WritePin(GPIOB, ESP_RESET_Pin, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(GPIOB, ESP_RESET_Pin, GPIO_PIN_SET);
    HAL_Delay(500);
}
/* USER CODE END 4 */

/* =============================================================================
 * Error handler
 * ============================================================================= */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    (void)file;
    (void)line;
    /* USER CODE END 6 */
}
#endif
