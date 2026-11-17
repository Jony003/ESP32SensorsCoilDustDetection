#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>
#include <Adafruit_APDS9960.h>
#include <SparkFun_AS7343.h>

Adafruit_APDS9960 apds;
SfeAS7343ArdI2C as7343;

// SDP810-125Pa：挂在默认 Wire 上（地址 0x25，与 AS7343 的 0x39 不冲突）
const uint8_t SDP_ADDR = 0x25;
const uint16_t SDP_CMD_STOP = 0x3FF9;
const uint16_t SDP_CMD_START_DP_AVG = 0x3615;  // 差压温度补偿 + 读前平均
const int16_t SDP_SCALE_125PA = 240;            // 数据手册默认值；实际以传感器回报为准
bool sdpOk = false;
int16_t sdpScale = SDP_SCALE_125PA;

// AS7343 通道，按波长从短到长排列
const sfe_as7343_channel_t order[] = {
  CH_PURPLE_F1_405NM, CH_DARK_BLUE_F2_425NM, CH_BLUE_FZ_450NM,
  CH_LIGHT_BLUE_F3_475NM, CH_BLUE_F4_515NM, CH_GREEN_F5_550NM,
  CH_GREEN_FY_555NM, CH_ORANGE_FXL_600NM, CH_BROWN_F6_640NM,
  CH_RED_F7_690NM, CH_DARK_RED_F8_745NM, CH_NIR_855NM, CH_VIS_1
};
const int N = sizeof(order) / sizeof(order[0]);

void as7343Write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(0x39);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void halt(const char *msg) {
  Serial.println(msg);
  while (1) delay(10);
}

// CRC-8：多项式 0x31，初值 0xFF，数据手册示例 CRC(0xBEEF) = 0x92
uint8_t sdpCrc8(uint8_t msb, uint8_t lsb) {
  uint8_t crc = 0xFF;
  uint8_t data[2] = {msb, lsb};
  for (uint8_t i = 0; i < 2; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x31);
      else crc <<= 1;
    }
  }
  return crc;
}

bool sdpWriteCmd(uint16_t cmd) {
  Wire.beginTransmission(SDP_ADDR);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

bool sdpBegin() {
  Wire.beginTransmission(SDP_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("SDP810 not found");
    return false;
  }
  sdpWriteCmd(SDP_CMD_STOP);  // 若已在连续测量，需先停止才能发新命令
  delay(2);
  if (!sdpWriteCmd(SDP_CMD_START_DP_AVG)) {
    Serial.println("SDP810 start failed");
    return false;
  }
  delay(10);  // 启动后约 8ms 才有第一个结果
  Serial.println("SDP810 started");
  return true;
}

// 读差压(Pa)和温度(°C)。失败时返回 false，调用方输出 nan。
bool sdpRead(float *dpPa, float *tempC) {
  uint8_t n = Wire.requestFrom(SDP_ADDR, (uint8_t)9);
  if (n != 9) return false;

  uint8_t buf[9];
  for (uint8_t i = 0; i < 9; i++) buf[i] = Wire.read();

  if (sdpCrc8(buf[0], buf[1]) != buf[2]) return false;
  if (sdpCrc8(buf[3], buf[4]) != buf[5]) return false;

  int16_t dpRaw = (int16_t)((buf[0] << 8) | buf[1]);
  int16_t tRaw = (int16_t)((buf[3] << 8) | buf[4]);
  if (sdpCrc8(buf[6], buf[7]) == buf[8]) {
    int16_t scale = (int16_t)((buf[6] << 8) | buf[7]);
    if (scale != 0) sdpScale = scale;
  }

  *dpPa = (float)dpRaw / (float)sdpScale;
  *tempC = (float)tRaw / 200.0f;
  return true;
}

void setup() {
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
  delay(100);
  Serial.begin(115200);
  delay(1000);

  Wire.begin();          // AS7343 + SDP810: 默认 SDA/SCL
  Wire1.begin(33, 32);   // APDS9960: SDA=33, SCL=32

  if (!apds.begin(200, APDS9960_AGAIN_64X, APDS9960_ADDRESS, &Wire1)) halt("APDS9960 not found");
  apds.enableColor(true);

  if (!as7343.begin(kAS7343Addr, Wire)) halt("AS7343 not found");
  if (!as7343.powerOn()) halt("AS7343 power on failed");
  if (!as7343.setAutoSmux(AUTOSMUX_18_CHANNELS)) halt("AS7343 smux failed");

  // 积分时间 = (ATIME+1) x (ASTEP+1) x 2.78us = 30 x 600 x 2.78us ≈ 50ms
  as7343Write(0x81, 29);          // ATIME
  as7343Write(0xD4, 599 & 0xFF);  // ASTEP 低 8 位
  as7343Write(0xD5, 599 >> 8);    // ASTEP 高 8 位
  as7343.setAgain(AGAIN_256);     // 增益从 0.5x 提到 256x
  if (!as7343.enableSpectralMeasurement()) halt("AS7343 enable failed");
  as7343.setLedDrive(10);         // LED 电流约 24mA，默认是 12mA
  as7343.ledOff();        // 打开板载白光 LED 照明

  // SDP810 找不到时只告警，不中断另外两个传感器
  sdpOk = sdpBegin();
}

void loop() {
  uint16_t r, g, b, c;
  while (!apds.colorDataReady()) delay(2);
  apds.getColorData(&r, &g, &b, &c);

  as7343.readSpectraDataFromSensor();

  float dpPa = NAN;
  float tempC = NAN;
  if (sdpOk) sdpRead(&dpPa, &tempC);

  // 一行输出：前 4 个是 APDS9960 的 R,G,B,Clear，接着 13 个是 AS7343，最后 2 个是 SDP810 的差压(Pa)和温度(°C)
  Serial.printf("%u,%u,%u,%u", r, g, b, c);
  for (int i = 0; i < N; i++) {
    Serial.printf(",%u", as7343.getChannelData(order[i]));
  }
  if (isnan(dpPa) || isnan(tempC)) {
    Serial.println(",nan,nan");
  } else {
    Serial.printf(",%.2f,%.2f\n", dpPa, tempC);
  }
}
