// 此文件由 generate_sensor_profiles.py 自动生成，请勿手工修改。
// 配置来源：config/sensor_profiles.json

const char kBuiltInProfileConfigVersion[] = "2026.07.23.1";

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

const SensorPollBlock kPn56377Blocks[] = {
  {45050, 3},
  {46001, 2},
  {46033, 8},
};

const SensorField kPn56377Fields[] = {
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

const SensorPollBlock kPn53001Blocks[] = {
  {46001, 14},
  {48001, 10},
};

const SensorField kPn53001Fields[] = {
  {"pH", 0, 0, SensorValueType::Float32CDAB, 2},
  {"pH_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"temperature", 0, 4, SensorValueType::Float32CDAB, 2},
  {"ORP", 0, 6, SensorValueType::Float32CDAB, 2},
  {"ORP_mA", 0, 8, SensorValueType::Float32CDAB, 2},
  {"pt100Res", 0, 10, SensorValueType::Float32CDAB, 2},
  {"ORP_mv", 0, 12, SensorValueType::Float32CDAB, 2},
  {"slope_acid", 1, 0, SensorValueType::Float32CDAB, 2},
  {"intercept_acid", 1, 2, SensorValueType::Float32CDAB, 2},
  {"adc_data", 1, 4, SensorValueType::Float32CDAB, 2},
  {"pH_V_origin", 1, 6, SensorValueType::Float32CDAB, 2},
  {"pointA_E", 1, 8, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn53607Blocks[] = {
  {46001, 22},
  {46025, 8},
  {46047, 2},
};

const SensorField kPn53607Fields[] = {
  {"Work nA", 0, 0, SensorValueType::Float32CDAB, 2},
  {"CL ppm", 0, 2, SensorValueType::Float32CDAB, 2},
  {"pH mV", 0, 4, SensorValueType::Float32CDAB, 2},
  {"pH", 0, 6, SensorValueType::Float32CDAB, 2},
  {"Temp C", 0, 8, SensorValueType::Float32CDAB, 2},
  {"_4_20mA", 0, 10, SensorValueType::Float32CDAB, 2},
  {"pH 4-20mA", 1, 2, SensorValueType::Float32CDAB, 2},
  {"ORP Vot", 0, 16, SensorValueType::Float32CDAB, 2},
  {"ORPValue", 0, 18, SensorValueType::Float32CDAB, 2},
  {"Rtd ohm", 0, 12, SensorValueType::Float32CDAB, 2},
  {"Pulse", 0, 14, SensorValueType::Float32CDAB, 2},
  {"CvCleanWorknA", 0, 20, SensorValueType::Float32CDAB, 2},
  {"CvClean_ppm", 2, 0, SensorValueType::Float32CDAB, 2},
  {"Count_Vot_mV", 1, 0, SensorValueType::Float32CDAB, 2},
  {"RawWork_nA", 1, 4, SensorValueType::Float32CDAB, 2},
  {"SesFilterWork_nA", 1, 6, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn53251Blocks[] = {
  {46001, 6},
  {48002, 12},
  {48021, 3},
  {48027, 3},
};

const SensorField kPn53251Fields[] = {
  {"sensorValue", 0, 0, SensorValueType::Float32CDAB, 2},
  {"sensorValue_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"temperature", 0, 4, SensorValueType::Float32CDAB, 2},
  {"turbiditySlopeLow", 1, 0, SensorValueType::Float32CDAB, 2},
  {"turbidityInterceptLow", 1, 2, SensorValueType::Float32CDAB, 2},
  {"turbiditySlopeMid", 1, 4, SensorValueType::Float32CDAB, 2},
  {"turbidityInterceptMid", 1, 6, SensorValueType::Float32CDAB, 2},
  {"turbiditySlopeHigh", 1, 8, SensorValueType::Float32CDAB, 2},
  {"turbidityInterceptHigh", 1, 10, SensorValueType::Float32CDAB, 2},
  {"s365Low", 2, 0, SensorValueType::UInt16, 0},
  {"s365Mid", 2, 1, SensorValueType::UInt16, 0},
  {"s365High", 2, 2, SensorValueType::UInt16, 0},
  {"s365DiLow", 3, 0, SensorValueType::UInt16, 0},
  {"s365DiMid", 3, 1, SensorValueType::UInt16, 0},
  {"s365DiHigh", 3, 2, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn50661Blocks[] = {
  {1000, 14},
  {1017, 8},
};

const SensorField kPn50661Fields[] = {
  {"f1", 0, 0, SensorValueType::Float32CDAB, 2},
  {"t1", 0, 2, SensorValueType::Float32CDAB, 2},
  {"t2", 0, 4, SensorValueType::Float32CDAB, 2},
  {"ppb", 0, 6, SensorValueType::Float32CDAB, 2},
  {"p1", 0, 8, SensorValueType::Float32CDAB, 2},
  {"p2", 0, 10, SensorValueType::Float32CDAB, 2},
  {"turbidity", 0, 12, SensorValueType::Float32CDAB, 2},
  {"t365Di", 1, 0, SensorValueType::UInt16, 1},
  {"s365Di", 1, 1, SensorValueType::UInt16, 1},
  {"t420Di", 1, 2, SensorValueType::UInt16, 1},
  {"s420Di", 1, 3, SensorValueType::UInt16, 1},
  {"t365", 1, 4, SensorValueType::UInt16, 1},
  {"s365", 1, 5, SensorValueType::UInt16, 1},
  {"t420", 1, 6, SensorValueType::UInt16, 1},
  {"s420", 1, 7, SensorValueType::UInt16, 1},
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
    56377,
    "ST-730SS-T",
    kPn56377Blocks,
    sizeof(kPn56377Blocks) / sizeof(kPn56377Blocks[0]),
    kPn56377Fields,
    sizeof(kPn56377Fields) / sizeof(kPn56377Fields[0])
  },
  {
    56600,
    "ST-730SS-T",
    kPn56600Blocks,
    sizeof(kPn56600Blocks) / sizeof(kPn56600Blocks[0]),
    kPn56600Fields,
    sizeof(kPn56600Fields) / sizeof(kPn56600Fields[0])
  },
  {
    53001,
    "ST-710",
    kPn53001Blocks,
    sizeof(kPn53001Blocks) / sizeof(kPn53001Blocks[0]),
    kPn53001Fields,
    sizeof(kPn53001Fields) / sizeof(kPn53001Fields[0])
  },
  {
    53607,
    "ST-765SS-FCL",
    kPn53607Blocks,
    sizeof(kPn53607Blocks) / sizeof(kPn53607Blocks[0]),
    kPn53607Fields,
    sizeof(kPn53607Fields) / sizeof(kPn53607Fields[0])
  },
  {
    53251,
    "LT-635",
    kPn53251Blocks,
    sizeof(kPn53251Blocks) / sizeof(kPn53251Blocks[0]),
    kPn53251Fields,
    sizeof(kPn53251Fields) / sizeof(kPn53251Fields[0])
  },
  {
    50661,
    "ST-500",
    kPn50661Blocks,
    sizeof(kPn50661Blocks) / sizeof(kPn50661Blocks[0]),
    kPn50661Fields,
    sizeof(kPn50661Fields) / sizeof(kPn50661Fields[0])
  },
};
