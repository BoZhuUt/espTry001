// 此文件由 generate_sensor_profiles.py 自动生成，请勿手工修改。
// 配置来源：config/sensor_profiles.json

const char kBuiltInProfileConfigVersion[] = "2026.07.19.1";

const SensorPollBlock kPn55995Blocks[] = {
  {45050, 3},
  {46001, 2},
  {46033, 8},
};

const SensorField kPn55995Fields[] = {
  {"sensorValue", 1, 0, SensorValueType::Float32CDAB, 3},
  {"s365LowRaw", 2, 0, SensorValueType::Float32CDAB, 2},
  {"s365HighRaw", 2, 2, SensorValueType::Float32CDAB, 2},
  {"s470Raw", 2, 4, SensorValueType::Float32CDAB, 2},
  {"t365Raw", 2, 6, SensorValueType::Float32CDAB, 2},
  {"s365LowZero_DI", 0, 0, SensorValueType::UInt16, 0},
  {"s365HighZero_DI", 0, 1, SensorValueType::UInt16, 0},
  {"t365Zero_DI", 0, 2, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn56600Blocks[] = {
  {45050, 3},
  {46001, 2},
  {46033, 8},
};

const SensorField kPn56600Fields[] = {
  {"sensorValue", 1, 0, SensorValueType::Float32CDAB, 3},
  {"s365LowRaw", 2, 0, SensorValueType::Float32CDAB, 2},
  {"s365HighRaw", 2, 2, SensorValueType::Float32CDAB, 2},
  {"s470Raw", 2, 4, SensorValueType::Float32CDAB, 2},
  {"t365Raw", 2, 6, SensorValueType::Float32CDAB, 2},
  {"s365LowZero_DI", 0, 0, SensorValueType::UInt16, 0},
  {"s365HighZero_DI", 0, 1, SensorValueType::UInt16, 0},
  {"t365Zero_DI", 0, 2, SensorValueType::UInt16, 0},
};

const SensorProfile kProfiles[] = {
  {
    55995,
    "ST-731SS-T",
    kPn55995Blocks,
    sizeof(kPn55995Blocks) / sizeof(kPn55995Blocks[0]),
    kPn55995Fields,
    sizeof(kPn55995Fields) / sizeof(kPn55995Fields[0])
  },
  {
    56600,
    "ST-730SS-T",
    kPn56600Blocks,
    sizeof(kPn56600Blocks) / sizeof(kPn56600Blocks[0]),
    kPn56600Fields,
    sizeof(kPn56600Fields) / sizeof(kPn56600Fields[0])
  },
};
