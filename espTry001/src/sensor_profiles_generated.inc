// 此文件由 generate_sensor_profiles.py 自动生成，请勿手工修改。
// 配置来源：config/sensor_profiles.json

const char kBuiltInProfileConfigVersion[] = "2026.08.27.1";

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

const SensorPollBlock kPn53003Blocks[] = {
  {46001, 14},
  {48001, 10},
};

const SensorField kPn53003Fields[] = {
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

const SensorPollBlock kPn53002Blocks[] = {
  {46005, 6},
  {46013, 2},
};

const SensorField kPn53002Fields[] = {
  {"temperature", 0, 0, SensorValueType::Float32CDAB, 2},
  {"ORP", 0, 2, SensorValueType::Float32CDAB, 2},
  {"ORP_mA", 0, 4, SensorValueType::Float32CDAB, 2},
  {"ORP_mv", 1, 0, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn53001Blocks[] = {
  {46001, 6},
  {46011, 2},
  {48001, 10},
};

const SensorField kPn53001Fields[] = {
  {"pH", 0, 0, SensorValueType::Float32CDAB, 2},
  {"pH_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"temperature", 0, 4, SensorValueType::Float32CDAB, 2},
  {"pt100Res", 1, 0, SensorValueType::Float32CDAB, 2},
  {"slope_acid", 2, 0, SensorValueType::Float32CDAB, 2},
  {"intercept_acid", 2, 2, SensorValueType::Float32CDAB, 2},
  {"adc_data", 2, 4, SensorValueType::Float32CDAB, 2},
  {"pH_V_origin", 2, 6, SensorValueType::Float32CDAB, 2},
  {"pointA_E", 2, 8, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn53004Blocks[] = {
  {46001, 6},
  {46011, 2},
  {48001, 10},
};

const SensorField kPn53004Fields[] = {
  {"pH", 0, 0, SensorValueType::Float32CDAB, 2},
  {"pH_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"temperature", 0, 4, SensorValueType::Float32CDAB, 2},
  {"pt100Res", 1, 0, SensorValueType::Float32CDAB, 2},
  {"slope_acid", 2, 0, SensorValueType::Float32CDAB, 2},
  {"intercept_acid", 2, 2, SensorValueType::Float32CDAB, 2},
  {"adc_data", 2, 4, SensorValueType::Float32CDAB, 2},
  {"pH_V_origin", 2, 6, SensorValueType::Float32CDAB, 2},
  {"pointA_E", 2, 8, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn53016Blocks[] = {
  {46001, 6},
  {46011, 2},
  {48001, 10},
};

const SensorField kPn53016Fields[] = {
  {"pH", 0, 0, SensorValueType::Float32CDAB, 2},
  {"pH_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"temperature", 0, 4, SensorValueType::Float32CDAB, 2},
  {"pt100Res", 1, 0, SensorValueType::Float32CDAB, 2},
  {"slope_acid", 2, 0, SensorValueType::Float32CDAB, 2},
  {"intercept_acid", 2, 2, SensorValueType::Float32CDAB, 2},
  {"adc_data", 2, 4, SensorValueType::Float32CDAB, 2},
  {"pH_V_origin", 2, 6, SensorValueType::Float32CDAB, 2},
  {"pointA_E", 2, 8, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn51761Blocks[] = {
  {46001, 6},
  {46011, 2},
  {48001, 10},
};

const SensorField kPn51761Fields[] = {
  {"pH", 0, 0, SensorValueType::Float32CDAB, 2},
  {"pH_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"temperature", 0, 4, SensorValueType::Float32CDAB, 2},
  {"pt100Res", 1, 0, SensorValueType::Float32CDAB, 2},
  {"slope_acid", 2, 0, SensorValueType::Float32CDAB, 2},
  {"intercept_acid", 2, 2, SensorValueType::Float32CDAB, 2},
  {"adc_data", 2, 4, SensorValueType::Float32CDAB, 2},
  {"pH_V_origin", 2, 6, SensorValueType::Float32CDAB, 2},
  {"pointA_E", 2, 8, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn53251Blocks[] = {
  {46001, 6},
  {48002, 12},
  {48021, 3},
  {48027, 3},
};

const SensorField kPn53251Fields[] = {
  {"turbidity", 0, 0, SensorValueType::Float32CDAB, 2},
  {"turbidity_mA", 0, 2, SensorValueType::Float32CDAB, 2},
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

const SensorPollBlock kPn10002Blocks[] = {
  {46001, 6},
  {48002, 12},
  {48021, 3},
  {48027, 3},
};

const SensorField kPn10002Fields[] = {
  {"turbidity", 0, 0, SensorValueType::Float32CDAB, 2},
  {"turbidity_mA", 0, 2, SensorValueType::Float32CDAB, 2},
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

const SensorPollBlock kPn53249Blocks[] = {
  {46001, 6},
  {48002, 12},
  {48021, 1},
  {48024, 1},
  {48027, 6},
  {48047, 1},
  {48051, 2},
};

const SensorField kPn53249Fields[] = {
  {"turbidity", 0, 0, SensorValueType::Float32CDAB, 2},
  {"turbidity_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"temperature", 0, 4, SensorValueType::Float32CDAB, 2},
  {"SlopeLow", 1, 0, SensorValueType::Float32CDAB, 2},
  {"InterceptLow", 1, 2, SensorValueType::Float32CDAB, 2},
  {"SlopeHigh", 1, 6, SensorValueType::Float32CDAB, 2},
  {"InterceptHigh", 6, 0, SensorValueType::Float32CDAB, 2},
  {"s365Low", 2, 0, SensorValueType::UInt16, 0},
  {"s365DarkLow", 3, 0, SensorValueType::UInt16, 0},
  {"s850Low", 5, 0, SensorValueType::UInt16, 0},
  {"highA", 1, 4, SensorValueType::Float32CDAB, 2},
  {"highB", 1, 8, SensorValueType::Float32CDAB, 2},
  {"highC", 1, 10, SensorValueType::Float32CDAB, 2},
  {"lowA", 4, 0, SensorValueType::Float32CDAB, 2},
  {"lowB", 4, 2, SensorValueType::Float32CDAB, 2},
  {"lowC", 4, 4, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn53250Blocks[] = {
  {46001, 6},
  {48002, 12},
  {48021, 1},
  {48024, 1},
  {48027, 6},
  {48047, 1},
  {48051, 2},
};

const SensorField kPn53250Fields[] = {
  {"turbidity", 0, 0, SensorValueType::Float32CDAB, 2},
  {"turbidity_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"temperature", 0, 4, SensorValueType::Float32CDAB, 2},
  {"SlopeLow", 1, 0, SensorValueType::Float32CDAB, 2},
  {"InterceptLow", 1, 2, SensorValueType::Float32CDAB, 2},
  {"SlopeHigh", 1, 6, SensorValueType::Float32CDAB, 2},
  {"InterceptHigh", 6, 0, SensorValueType::Float32CDAB, 2},
  {"s365Low", 2, 0, SensorValueType::UInt16, 0},
  {"s365DarkLow", 3, 0, SensorValueType::UInt16, 0},
  {"s850Low", 5, 0, SensorValueType::UInt16, 0},
  {"highA", 1, 4, SensorValueType::Float32CDAB, 2},
  {"highB", 1, 8, SensorValueType::Float32CDAB, 2},
  {"highC", 1, 10, SensorValueType::Float32CDAB, 2},
  {"lowA", 4, 0, SensorValueType::Float32CDAB, 2},
  {"lowB", 4, 2, SensorValueType::Float32CDAB, 2},
  {"lowC", 4, 4, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn53252Blocks[] = {
  {45011, 2},
  {46001, 4},
  {48002, 8},
  {48016, 1},
  {48018, 1},
  {48020, 1},
  {48025, 6},
};

const SensorField kPn53252Fields[] = {
  {"turbidity", 1, 0, SensorValueType::Float32CDAB, 2},
  {"turbidity_mA", 1, 2, SensorValueType::Float32CDAB, 2},
  {"SlopeLow", 2, 0, SensorValueType::Float32CDAB, 2},
  {"slopeMid", 2, 2, SensorValueType::Float32CDAB, 2},
  {"SlopeHigh", 2, 4, SensorValueType::Float32CDAB, 2},
  {"Intercept", 2, 6, SensorValueType::Float32CDAB, 2},
  {"s365di_LOW", 3, 0, SensorValueType::UInt16, 0},
  {"s365di_MID", 6, 0, SensorValueType::UInt16, 0},
  {"s365di_HIGH", 6, 1, SensorValueType::UInt16, 0},
  {"s365_LOW", 4, 0, SensorValueType::UInt16, 0},
  {"s365_MID", 6, 2, SensorValueType::UInt16, 0},
  {"s365_HIGH", 6, 3, SensorValueType::UInt16, 0},
  {"s365Dark_LOW", 5, 0, SensorValueType::UInt16, 0},
  {"s365Dark_MID", 6, 4, SensorValueType::UInt16, 0},
  {"s365Dark_HIGH", 6, 5, SensorValueType::UInt16, 0},
  {"offset", 0, 0, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn53215Blocks[] = {
  {46001, 4},
  {48002, 8},
  {48015, 2},
  {48018, 1},
  {48020, 1},
  {48027, 8},
};

const SensorField kPn53215Fields[] = {
  {"turbidity", 0, 0, SensorValueType::Float32CDAB, 2},
  {"turbidity_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"SlopeLow", 1, 0, SensorValueType::Float32CDAB, 2},
  {"slopeMid", 1, 2, SensorValueType::Float32CDAB, 2},
  {"SlopeHigh", 1, 4, SensorValueType::Float32CDAB, 2},
  {"InterceptLow", 1, 6, SensorValueType::Float32CDAB, 2},
  {"interceptMid", 5, 4, SensorValueType::Float32CDAB, 2},
  {"interceptHigh", 5, 6, SensorValueType::Float32CDAB, 2},
  {"s365_LOW", 3, 0, SensorValueType::UInt16, 0},
  {"s365_MID", 5, 0, SensorValueType::UInt16, 0},
  {"s365_HIGH", 5, 1, SensorValueType::UInt16, 0},
  {"s365Dark_LOW", 4, 0, SensorValueType::UInt16, 0},
  {"s365Dark_MID", 5, 2, SensorValueType::UInt16, 0},
  {"s365Dark_HIGH", 5, 3, SensorValueType::UInt16, 0},
  {"offset", 2, 0, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn53216Blocks[] = {
  {1000, 8},
  {1014, 2},
  {1017, 1},
  {1020, 5},
  {1043, 1},
  {1052, 2},
  {1056, 4},
};

const SensorField kPn53216Fields[] = {
  {"turbidity", 0, 6, SensorValueType::Float32CDAB, 2},
  {"turbidity_mA", 1, 0, SensorValueType::Float32CDAB, 2},
  {"SlopeLow", 0, 0, SensorValueType::Float32CDAB, 2},
  {"SlopeHigh", 0, 2, SensorValueType::Float32CDAB, 2},
  {"InterceptLow", 6, 0, SensorValueType::Float32CDAB, 2},
  {"interceptHigh", 0, 4, SensorValueType::Float32CDAB, 2},
  {"s365_LOW", 3, 2, SensorValueType::UInt16, 0},
  {"s365_HIGH", 3, 4, SensorValueType::UInt16, 0},
  {"s365Dark", 4, 0, SensorValueType::UInt16, 0},
  {"t365Di", 2, 0, SensorValueType::UInt16, 0},
  {"t365", 3, 1, SensorValueType::UInt16, 0},
  {"offset", 6, 2, SensorValueType::Float32CDAB, 2},
  {"s365LowCalib", 5, 0, SensorValueType::UInt16, 0},
  {"s365MidLowCalib", 3, 0, SensorValueType::UInt16, 0},
  {"s365MidHighCalib", 3, 3, SensorValueType::UInt16, 0},
  {"s365HighCalib", 5, 1, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn53224Blocks[] = {
  {1000, 8},
  {1014, 2},
  {1017, 1},
  {1020, 5},
  {1043, 1},
  {1052, 2},
  {1056, 4},
};

const SensorField kPn53224Fields[] = {
  {"turbidity", 0, 6, SensorValueType::Float32CDAB, 2},
  {"turbidity_mA", 1, 0, SensorValueType::Float32CDAB, 2},
  {"SlopeLow", 0, 0, SensorValueType::Float32CDAB, 2},
  {"SlopeHigh", 0, 2, SensorValueType::Float32CDAB, 2},
  {"InterceptLow", 6, 0, SensorValueType::Float32CDAB, 2},
  {"interceptHigh", 0, 4, SensorValueType::Float32CDAB, 2},
  {"s365_LOW", 3, 2, SensorValueType::UInt16, 0},
  {"s365_HIGH", 3, 4, SensorValueType::UInt16, 0},
  {"s365Dark", 4, 0, SensorValueType::UInt16, 0},
  {"t365Di", 2, 0, SensorValueType::UInt16, 0},
  {"t365", 3, 1, SensorValueType::UInt16, 0},
  {"offset", 6, 2, SensorValueType::Float32CDAB, 2},
  {"s365LowCalib", 5, 0, SensorValueType::UInt16, 0},
  {"s365MidLowCalib", 3, 0, SensorValueType::UInt16, 0},
  {"s365MidHighCalib", 3, 3, SensorValueType::UInt16, 0},
  {"s365HighCalib", 5, 1, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn53221Blocks[] = {
  {1000, 8},
  {1014, 2},
  {1017, 1},
  {1020, 5},
  {1043, 1},
  {1052, 2},
  {1056, 4},
};

const SensorField kPn53221Fields[] = {
  {"turbidity", 0, 6, SensorValueType::Float32CDAB, 2},
  {"turbidity_mA", 1, 0, SensorValueType::Float32CDAB, 2},
  {"SlopeLow", 0, 0, SensorValueType::Float32CDAB, 2},
  {"SlopeHigh", 0, 2, SensorValueType::Float32CDAB, 2},
  {"InterceptLow", 6, 0, SensorValueType::Float32CDAB, 2},
  {"interceptHigh", 0, 4, SensorValueType::Float32CDAB, 2},
  {"s365_LOW", 3, 2, SensorValueType::UInt16, 0},
  {"s365_HIGH", 3, 4, SensorValueType::UInt16, 0},
  {"s365Dark", 4, 0, SensorValueType::UInt16, 0},
  {"t365Di", 2, 0, SensorValueType::UInt16, 0},
  {"t365", 3, 1, SensorValueType::UInt16, 0},
  {"offset", 6, 2, SensorValueType::Float32CDAB, 2},
  {"s365LowCalib", 5, 0, SensorValueType::UInt16, 0},
  {"s365MidLowCalib", 3, 0, SensorValueType::UInt16, 0},
  {"s365MidHighCalib", 3, 3, SensorValueType::UInt16, 0},
  {"s365HighCalib", 5, 1, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn53225Blocks[] = {
  {1000, 8},
  {1014, 2},
  {1017, 1},
  {1020, 5},
  {1043, 1},
  {1052, 2},
  {1056, 4},
};

const SensorField kPn53225Fields[] = {
  {"turbidity", 0, 6, SensorValueType::Float32CDAB, 2},
  {"turbidity_mA", 1, 0, SensorValueType::Float32CDAB, 2},
  {"SlopeLow", 0, 0, SensorValueType::Float32CDAB, 2},
  {"SlopeHigh", 0, 2, SensorValueType::Float32CDAB, 2},
  {"InterceptLow", 6, 0, SensorValueType::Float32CDAB, 2},
  {"interceptHigh", 0, 4, SensorValueType::Float32CDAB, 2},
  {"s365_LOW", 3, 2, SensorValueType::UInt16, 0},
  {"s365_HIGH", 3, 4, SensorValueType::UInt16, 0},
  {"s365Dark", 4, 0, SensorValueType::UInt16, 0},
  {"t365Di", 2, 0, SensorValueType::UInt16, 0},
  {"t365", 3, 1, SensorValueType::UInt16, 0},
  {"offset", 6, 2, SensorValueType::Float32CDAB, 2},
  {"s365LowCalib", 5, 0, SensorValueType::UInt16, 0},
  {"s365MidLowCalib", 3, 0, SensorValueType::UInt16, 0},
  {"s365MidHighCalib", 3, 3, SensorValueType::UInt16, 0},
  {"s365HighCalib", 5, 1, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn53223Blocks[] = {
  {46001, 4},
  {48002, 8},
  {48015, 2},
  {48018, 1},
  {48020, 1},
  {48027, 8},
};

const SensorField kPn53223Fields[] = {
  {"turbidity", 0, 0, SensorValueType::Float32CDAB, 2},
  {"turbidity_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"SlopeLow", 1, 0, SensorValueType::Float32CDAB, 2},
  {"slopeMid", 1, 2, SensorValueType::Float32CDAB, 2},
  {"SlopeHigh", 1, 4, SensorValueType::Float32CDAB, 2},
  {"InterceptLow", 1, 6, SensorValueType::Float32CDAB, 2},
  {"interceptMid", 5, 4, SensorValueType::Float32CDAB, 2},
  {"interceptHigh", 5, 6, SensorValueType::Float32CDAB, 2},
  {"s365_LOW", 3, 0, SensorValueType::UInt16, 0},
  {"s365_MID", 5, 0, SensorValueType::UInt16, 0},
  {"s365_HIGH", 5, 1, SensorValueType::UInt16, 0},
  {"s365Dark_LOW", 4, 0, SensorValueType::UInt16, 0},
  {"s365Dark_MID", 5, 2, SensorValueType::UInt16, 0},
  {"s365Dark_HIGH", 5, 3, SensorValueType::UInt16, 0},
  {"offset", 2, 0, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn50692Blocks[] = {
  {46001, 4},
  {46007, 4},
  {48002, 2},
  {48010, 2},
  {48030, 4},
  {48035, 4},
  {48041, 1},
  {48043, 1},
};

const SensorField kPn50692Fields[] = {
  {"PTSA", 0, 0, SensorValueType::Float32CDAB, 2},
  {"PTSA_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"TAG", 1, 0, SensorValueType::Float32CDAB, 2},
  {"TAG_mA", 1, 2, SensorValueType::Float32CDAB, 2},
  {"S365Dark", 6, 0, SensorValueType::UInt16, 0},
  {"S470Dark", 7, 0, SensorValueType::UInt16, 0},
  {"T365DI", 4, 0, SensorValueType::UInt16, 0},
  {"S365DI", 4, 1, SensorValueType::UInt16, 0},
  {"S410DI", 4, 2, SensorValueType::UInt16, 0},
  {"S470DI", 4, 3, SensorValueType::UInt16, 0},
  {"T365", 5, 0, SensorValueType::UInt16, 0},
  {"S365", 5, 1, SensorValueType::UInt16, 0},
  {"S410", 5, 2, SensorValueType::UInt16, 0},
  {"S470", 5, 3, SensorValueType::UInt16, 0},
  {"PTSA Slope", 2, 0, SensorValueType::Float32CDAB, 2},
  {"Tag Slope", 3, 0, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn50501Blocks[] = {
  {46001, 4},
  {48002, 2},
  {48019, 8},
  {48028, 1},
  {48030, 1},
};

const SensorField kPn50501Fields[] = {
  {"Chlorophyll-a", 0, 0, SensorValueType::Float32CDAB, 2},
  {"Chlorophyll-a_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"S365Dark", 3, 0, SensorValueType::UInt16, 0},
  {"S470Dark", 4, 0, SensorValueType::UInt16, 0},
  {"T365DI", 2, 0, SensorValueType::UInt16, 0},
  {"S365DI", 2, 1, SensorValueType::UInt16, 0},
  {"S410DI", 2, 2, SensorValueType::UInt16, 0},
  {"S470DI", 2, 3, SensorValueType::UInt16, 0},
  {"T365", 2, 4, SensorValueType::UInt16, 0},
  {"S365", 2, 5, SensorValueType::UInt16, 0},
  {"S410", 2, 6, SensorValueType::UInt16, 0},
  {"S470", 2, 7, SensorValueType::UInt16, 0},
  {"PTSA Slope", 1, 0, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn50503Blocks[] = {
  {46001, 4},
  {48002, 2},
  {48019, 8},
  {48028, 1},
  {48030, 1},
};

const SensorField kPn50503Fields[] = {
  {"Algae", 0, 0, SensorValueType::Float32CDAB, 2},
  {"Algae_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"S365Dark", 3, 0, SensorValueType::UInt16, 0},
  {"S470Dark", 4, 0, SensorValueType::UInt16, 0},
  {"T365DI", 2, 0, SensorValueType::UInt16, 0},
  {"S365DI", 2, 1, SensorValueType::UInt16, 0},
  {"S410DI", 2, 2, SensorValueType::UInt16, 0},
  {"S470DI", 2, 3, SensorValueType::UInt16, 0},
  {"T365", 2, 4, SensorValueType::UInt16, 0},
  {"S365", 2, 5, SensorValueType::UInt16, 0},
  {"S410", 2, 6, SensorValueType::UInt16, 0},
  {"S470", 2, 7, SensorValueType::UInt16, 0},
  {"PTSA Slope", 1, 0, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn50522Blocks[] = {
  {46001, 4},
  {48002, 2},
  {48019, 8},
  {48028, 1},
  {48030, 1},
};

const SensorField kPn50522Fields[] = {
  {"Chlorophyll-a", 0, 0, SensorValueType::Float32CDAB, 2},
  {"Chlorophyll-a_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"S365Dark", 3, 0, SensorValueType::UInt16, 0},
  {"S470Dark", 4, 0, SensorValueType::UInt16, 0},
  {"T365DI", 2, 0, SensorValueType::UInt16, 0},
  {"S365DI", 2, 1, SensorValueType::UInt16, 0},
  {"S410DI", 2, 2, SensorValueType::UInt16, 0},
  {"S470DI", 2, 3, SensorValueType::UInt16, 0},
  {"T365", 2, 4, SensorValueType::UInt16, 0},
  {"S365", 2, 5, SensorValueType::UInt16, 0},
  {"S410", 2, 6, SensorValueType::UInt16, 0},
  {"S470", 2, 7, SensorValueType::UInt16, 0},
  {"PTSA Slope", 1, 0, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn52101Blocks[] = {
  {46001, 4},
  {46011, 1},
  {48002, 2},
  {48019, 8},
  {48028, 1},
  {48030, 1},
};

const SensorField kPn52101Fields[] = {
  {"OiW", 0, 0, SensorValueType::Float32CDAB, 2},
  {"OiW_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"S365Dark", 4, 0, SensorValueType::UInt16, 0},
  {"S470Dark", 5, 0, SensorValueType::UInt16, 0},
  {"T365DI", 3, 0, SensorValueType::UInt16, 0},
  {"S365DI", 3, 1, SensorValueType::UInt16, 0},
  {"S410DI", 3, 2, SensorValueType::UInt16, 0},
  {"S470DI", 3, 3, SensorValueType::UInt16, 0},
  {"T365", 3, 4, SensorValueType::UInt16, 0},
  {"S365", 3, 5, SensorValueType::UInt16, 0},
  {"S410", 3, 6, SensorValueType::UInt16, 0},
  {"S470", 3, 7, SensorValueType::UInt16, 0},
  {"PTSA Slope", 2, 0, SensorValueType::Float32CDAB, 2},
  {"cleanLiness", 1, 0, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn52102Blocks[] = {
  {46001, 4},
  {46011, 1},
  {48002, 2},
  {48019, 8},
  {48028, 1},
  {48030, 1},
};

const SensorField kPn52102Fields[] = {
  {"OiW", 0, 0, SensorValueType::Float32CDAB, 2},
  {"OiW_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"S365Dark", 4, 0, SensorValueType::UInt16, 0},
  {"S470Dark", 5, 0, SensorValueType::UInt16, 0},
  {"T365DI", 3, 0, SensorValueType::UInt16, 0},
  {"S365DI", 3, 1, SensorValueType::UInt16, 0},
  {"S410DI", 3, 2, SensorValueType::UInt16, 0},
  {"S470DI", 3, 3, SensorValueType::UInt16, 0},
  {"T365", 3, 4, SensorValueType::UInt16, 0},
  {"S365", 3, 5, SensorValueType::UInt16, 0},
  {"S410", 3, 6, SensorValueType::UInt16, 0},
  {"S470", 3, 7, SensorValueType::UInt16, 0},
  {"PTSA Slope", 2, 0, SensorValueType::Float32CDAB, 2},
  {"cleanLiness", 1, 0, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn52106Blocks[] = {
  {46001, 4},
  {46011, 1},
  {48002, 2},
  {48019, 8},
  {48028, 1},
  {48030, 1},
};

const SensorField kPn52106Fields[] = {
  {"OiW", 0, 0, SensorValueType::Float32CDAB, 2},
  {"OiW_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"S365Dark", 4, 0, SensorValueType::UInt16, 0},
  {"S470Dark", 5, 0, SensorValueType::UInt16, 0},
  {"T365DI", 3, 0, SensorValueType::UInt16, 0},
  {"S365DI", 3, 1, SensorValueType::UInt16, 0},
  {"S410DI", 3, 2, SensorValueType::UInt16, 0},
  {"S470DI", 3, 3, SensorValueType::UInt16, 0},
  {"T365", 3, 4, SensorValueType::UInt16, 0},
  {"S365", 3, 5, SensorValueType::UInt16, 0},
  {"S410", 3, 6, SensorValueType::UInt16, 0},
  {"S470", 3, 7, SensorValueType::UInt16, 0},
  {"PTSA Slope", 2, 0, SensorValueType::Float32CDAB, 2},
  {"cleanLiness", 1, 0, SensorValueType::UInt16, 0},
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
  {"t365Di", 1, 0, SensorValueType::UInt16, 0},
  {"s365Di", 1, 1, SensorValueType::UInt16, 0},
  {"t420Di", 1, 2, SensorValueType::UInt16, 0},
  {"s420Di", 1, 3, SensorValueType::UInt16, 0},
  {"t365", 1, 4, SensorValueType::UInt16, 0},
  {"s365", 1, 5, SensorValueType::UInt16, 0},
  {"t420", 1, 6, SensorValueType::UInt16, 0},
  {"s420", 1, 7, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn52411Blocks[] = {
  {1000, 1},
  {1006, 2},
  {1014, 2},
  {1017, 8},
  {1043, 1},
};

const SensorField kPn52411Fields[] = {
  {"PAH", 1, 0, SensorValueType::Float32CDAB, 2},
  {"PAH 4-20mA", 2, 0, SensorValueType::Float32CDAB, 2},
  {"S365Dark", 4, 0, SensorValueType::UInt16, 0},
  {"t365Di", 3, 0, SensorValueType::UInt16, 0},
  {"s365Di", 3, 1, SensorValueType::UInt16, 0},
  {"t420Di", 3, 2, SensorValueType::UInt16, 0},
  {"s420Di", 3, 3, SensorValueType::UInt16, 0},
  {"t365", 3, 4, SensorValueType::UInt16, 0},
  {"s365", 3, 5, SensorValueType::UInt16, 0},
  {"t420", 3, 6, SensorValueType::UInt16, 0},
  {"s420", 3, 7, SensorValueType::UInt16, 0},
  {"Slope", 0, 0, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn52217Blocks[] = {
  {1000, 1},
  {1006, 2},
  {1014, 2},
  {1017, 8},
  {1043, 1},
};

const SensorField kPn52217Fields[] = {
  {"Sugar", 1, 0, SensorValueType::Float32CDAB, 2},
  {"Sugar 4-20mA", 2, 0, SensorValueType::Float32CDAB, 2},
  {"S365Dark", 4, 0, SensorValueType::UInt16, 0},
  {"t365Di", 3, 0, SensorValueType::UInt16, 0},
  {"s365Di", 3, 1, SensorValueType::UInt16, 0},
  {"t420Di", 3, 2, SensorValueType::UInt16, 0},
  {"s420Di", 3, 3, SensorValueType::UInt16, 0},
  {"t365", 3, 4, SensorValueType::UInt16, 0},
  {"s365", 3, 5, SensorValueType::UInt16, 0},
  {"t420", 3, 6, SensorValueType::UInt16, 0},
  {"s420", 3, 7, SensorValueType::UInt16, 0},
  {"Slope", 0, 0, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn52111Blocks[] = {
  {1000, 2},
  {1006, 2},
  {1017, 8},
  {1043, 1},
};

const SensorField kPn52111Fields[] = {
  {"DOM", 1, 0, SensorValueType::Float32CDAB, 2},
  {"t365Di", 2, 0, SensorValueType::UInt16, 0},
  {"s365Di", 2, 1, SensorValueType::UInt16, 0},
  {"t420Di", 2, 2, SensorValueType::UInt16, 0},
  {"s420Di", 2, 3, SensorValueType::UInt16, 1},
  {"t365", 2, 4, SensorValueType::UInt16, 0},
  {"s365", 2, 5, SensorValueType::UInt16, 0},
  {"t420", 2, 6, SensorValueType::UInt16, 0},
  {"s420", 2, 7, SensorValueType::UInt16, 0},
  {"Slope", 0, 0, SensorValueType::Float32CDAB, 2},
  {"S365Dark", 3, 0, SensorValueType::UInt16, 0},
};

const SensorPollBlock kPn52112Blocks[] = {
  {1000, 2},
  {1006, 2},
  {1017, 8},
  {1043, 1},
};

const SensorField kPn52112Fields[] = {
  {"DOM", 1, 0, SensorValueType::Float32CDAB, 2},
  {"t365Di", 2, 0, SensorValueType::UInt16, 0},
  {"s365Di", 2, 1, SensorValueType::UInt16, 0},
  {"t420Di", 2, 2, SensorValueType::UInt16, 0},
  {"s420Di", 2, 3, SensorValueType::UInt16, 1},
  {"t365", 2, 4, SensorValueType::UInt16, 0},
  {"s365", 2, 5, SensorValueType::UInt16, 0},
  {"t420", 2, 6, SensorValueType::UInt16, 0},
  {"s420", 2, 7, SensorValueType::UInt16, 0},
  {"Slope", 0, 0, SensorValueType::Float32CDAB, 2},
  {"S365Dark", 3, 0, SensorValueType::UInt16, 0},
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

const SensorPollBlock kPn52267Blocks[] = {
  {46001, 4},
  {48002, 8},
  {48028, 16},
};

const SensorField kPn52267Fields[] = {
  {"PTSA", 0, 0, SensorValueType::Float32CDAB, 2},
  {"PTSA_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"t365 dark", 2, 8, SensorValueType::Float32CDAB, 2},
  {"s365 dark", 2, 10, SensorValueType::Float32CDAB, 2},
  {"t420 dark", 2, 12, SensorValueType::Float32CDAB, 2},
  {"s420 dark", 2, 14, SensorValueType::Float32CDAB, 2},
  {"T1", 1, 4, SensorValueType::Float32CDAB, 2},
  {"T2", 1, 6, SensorValueType::Float32CDAB, 2},
  {"t365", 2, 0, SensorValueType::Float32CDAB, 2},
  {"s365", 2, 2, SensorValueType::Float32CDAB, 2},
  {"t420", 2, 4, SensorValueType::Float32CDAB, 2},
  {"s420", 2, 6, SensorValueType::Float32CDAB, 2},
  {"Slope", 1, 0, SensorValueType::Float32CDAB, 2},
  {"intercept", 1, 2, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn53577Blocks[] = {
  {46001, 4},
  {48002, 8},
  {48028, 16},
};

const SensorField kPn53577Fields[] = {
  {"Chlorophyl-a", 0, 0, SensorValueType::Float32CDAB, 2},
  {"Chlorophyl-a_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"t365 dark", 2, 8, SensorValueType::Float32CDAB, 2},
  {"s365 dark", 2, 10, SensorValueType::Float32CDAB, 2},
  {"t420 dark", 2, 12, SensorValueType::Float32CDAB, 2},
  {"s420 dark", 2, 14, SensorValueType::Float32CDAB, 2},
  {"T1", 1, 4, SensorValueType::Float32CDAB, 2},
  {"T2", 1, 6, SensorValueType::Float32CDAB, 2},
  {"t365", 2, 0, SensorValueType::Float32CDAB, 2},
  {"s365", 2, 2, SensorValueType::Float32CDAB, 2},
  {"t420", 2, 4, SensorValueType::Float32CDAB, 2},
  {"s420", 2, 6, SensorValueType::Float32CDAB, 2},
  {"Slope", 1, 0, SensorValueType::Float32CDAB, 2},
  {"intercept", 1, 2, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn54548Blocks[] = {
  {46001, 4},
  {48002, 8},
  {48028, 16},
};

const SensorField kPn54548Fields[] = {
  {"OiW", 0, 0, SensorValueType::Float32CDAB, 2},
  {"OiW_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"t365 dark", 2, 8, SensorValueType::Float32CDAB, 2},
  {"s365 dark", 2, 10, SensorValueType::Float32CDAB, 2},
  {"t420 dark", 2, 12, SensorValueType::Float32CDAB, 2},
  {"s420 dark", 2, 14, SensorValueType::Float32CDAB, 2},
  {"T1", 1, 4, SensorValueType::Float32CDAB, 2},
  {"T2", 1, 6, SensorValueType::Float32CDAB, 2},
  {"t365", 2, 0, SensorValueType::Float32CDAB, 2},
  {"s365", 2, 2, SensorValueType::Float32CDAB, 2},
  {"t420", 2, 4, SensorValueType::Float32CDAB, 2},
  {"s420", 2, 6, SensorValueType::Float32CDAB, 2},
  {"Slope", 1, 0, SensorValueType::Float32CDAB, 2},
  {"intercept", 1, 2, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn57172Blocks[] = {
  {46001, 4},
  {48002, 8},
  {48028, 16},
};

const SensorField kPn57172Fields[] = {
  {"Chroma", 0, 0, SensorValueType::Float32CDAB, 2},
  {"Chroma_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"t365 dark", 2, 8, SensorValueType::Float32CDAB, 2},
  {"s365 dark", 2, 10, SensorValueType::Float32CDAB, 2},
  {"t420 dark", 2, 12, SensorValueType::Float32CDAB, 2},
  {"s420 dark", 2, 14, SensorValueType::Float32CDAB, 2},
  {"T1", 1, 4, SensorValueType::Float32CDAB, 2},
  {"T2", 1, 6, SensorValueType::Float32CDAB, 2},
  {"t365", 2, 0, SensorValueType::Float32CDAB, 2},
  {"s365", 2, 2, SensorValueType::Float32CDAB, 2},
  {"t420", 2, 4, SensorValueType::Float32CDAB, 2},
  {"s420", 2, 6, SensorValueType::Float32CDAB, 2},
  {"Slope", 1, 0, SensorValueType::Float32CDAB, 2},
  {"intercept", 1, 2, SensorValueType::Float32CDAB, 2},
};

const SensorPollBlock kPn58948Blocks[] = {
  {46001, 4},
  {48002, 8},
  {48020, 16},
};

const SensorField kPn58948Fields[] = {
  {"PTSA", 0, 0, SensorValueType::Float32CDAB, 2},
  {"PTSA_mA", 0, 2, SensorValueType::Float32CDAB, 2},
  {"t365Di", 2, 0, SensorValueType::Float32CDAB, 2},
  {"s365Di", 2, 2, SensorValueType::Float32CDAB, 2},
  {"t420Di", 2, 4, SensorValueType::Float32CDAB, 2},
  {"s420Di", 2, 6, SensorValueType::Float32CDAB, 2},
  {"T1", 1, 4, SensorValueType::Float32CDAB, 2},
  {"T2", 1, 6, SensorValueType::Float32CDAB, 2},
  {"t365", 2, 8, SensorValueType::Float32CDAB, 2},
  {"s365", 2, 10, SensorValueType::Float32CDAB, 2},
  {"t420", 2, 12, SensorValueType::Float32CDAB, 2},
  {"s420", 2, 14, SensorValueType::Float32CDAB, 2},
  {"Slope", 1, 0, SensorValueType::Float32CDAB, 2},
  {"intercept", 1, 2, SensorValueType::Float32CDAB, 2},
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
    53003,
    "ST-712",
    kPn53003Blocks,
    sizeof(kPn53003Blocks) / sizeof(kPn53003Blocks[0]),
    kPn53003Fields,
    sizeof(kPn53003Fields) / sizeof(kPn53003Fields[0])
  },
  {
    53002,
    "ST-711",
    kPn53002Blocks,
    sizeof(kPn53002Blocks) / sizeof(kPn53002Blocks[0]),
    kPn53002Fields,
    sizeof(kPn53002Fields) / sizeof(kPn53002Fields[0])
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
    53004,
    "ST-715-325",
    kPn53004Blocks,
    sizeof(kPn53004Blocks) / sizeof(kPn53004Blocks[0]),
    kPn53004Fields,
    sizeof(kPn53004Fields) / sizeof(kPn53004Fields[0])
  },
  {
    53016,
    "ST-715-120",
    kPn53016Blocks,
    sizeof(kPn53016Blocks) / sizeof(kPn53016Blocks[0]),
    kPn53016Fields,
    sizeof(kPn53016Fields) / sizeof(kPn53016Fields[0])
  },
  {
    51761,
    "ST-716-120",
    kPn51761Blocks,
    sizeof(kPn51761Blocks) / sizeof(kPn51761Blocks[0]),
    kPn51761Fields,
    sizeof(kPn51761Fields) / sizeof(kPn51761Fields[0])
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
    10002,
    "LT-633",
    kPn10002Blocks,
    sizeof(kPn10002Blocks) / sizeof(kPn10002Blocks[0]),
    kPn10002Fields,
    sizeof(kPn10002Fields) / sizeof(kPn10002Fields[0])
  },
  {
    53249,
    "LT-631",
    kPn53249Blocks,
    sizeof(kPn53249Blocks) / sizeof(kPn53249Blocks[0]),
    kPn53249Fields,
    sizeof(kPn53249Fields) / sizeof(kPn53249Fields[0])
  },
  {
    53250,
    "LT-632",
    kPn53250Blocks,
    sizeof(kPn53250Blocks) / sizeof(kPn53250Blocks[0]),
    kPn53250Fields,
    sizeof(kPn53250Fields) / sizeof(kPn53250Fields[0])
  },
  {
    53252,
    "LT-735",
    kPn53252Blocks,
    sizeof(kPn53252Blocks) / sizeof(kPn53252Blocks[0]),
    kPn53252Fields,
    sizeof(kPn53252Fields) / sizeof(kPn53252Fields[0])
  },
  {
    53215,
    "LT-736",
    kPn53215Blocks,
    sizeof(kPn53215Blocks) / sizeof(kPn53215Blocks[0]),
    kPn53215Fields,
    sizeof(kPn53215Fields) / sizeof(kPn53215Fields[0])
  },
  {
    53216,
    "LT-737",
    kPn53216Blocks,
    sizeof(kPn53216Blocks) / sizeof(kPn53216Blocks[0]),
    kPn53216Fields,
    sizeof(kPn53216Fields) / sizeof(kPn53216Fields[0])
  },
  {
    53224,
    "LT-737B",
    kPn53224Blocks,
    sizeof(kPn53224Blocks) / sizeof(kPn53224Blocks[0]),
    kPn53224Fields,
    sizeof(kPn53224Fields) / sizeof(kPn53224Fields[0])
  },
  {
    53221,
    "LT-739",
    kPn53221Blocks,
    sizeof(kPn53221Blocks) / sizeof(kPn53221Blocks[0]),
    kPn53221Fields,
    sizeof(kPn53221Fields) / sizeof(kPn53221Fields[0])
  },
  {
    53225,
    "LT-739B",
    kPn53225Blocks,
    sizeof(kPn53225Blocks) / sizeof(kPn53225Blocks[0]),
    kPn53225Fields,
    sizeof(kPn53225Fields) / sizeof(kPn53225Fields[0])
  },
  {
    53223,
    "LT-736B",
    kPn53223Blocks,
    sizeof(kPn53223Blocks) / sizeof(kPn53223Blocks[0]),
    kPn53223Fields,
    sizeof(kPn53223Fields) / sizeof(kPn53223Fields[0])
  },
  {
    50692,
    "ST-588",
    kPn50692Blocks,
    sizeof(kPn50692Blocks) / sizeof(kPn50692Blocks[0]),
    kPn50692Fields,
    sizeof(kPn50692Fields) / sizeof(kPn50692Fields[0])
  },
  {
    50501,
    "EM-500",
    kPn50501Blocks,
    sizeof(kPn50501Blocks) / sizeof(kPn50501Blocks[0]),
    kPn50501Fields,
    sizeof(kPn50501Fields) / sizeof(kPn50501Fields[0])
  },
  {
    50503,
    "EM-501",
    kPn50503Blocks,
    sizeof(kPn50503Blocks) / sizeof(kPn50503Blocks[0]),
    kPn50503Fields,
    sizeof(kPn50503Fields) / sizeof(kPn50503Fields[0])
  },
  {
    50522,
    "EM-510",
    kPn50522Blocks,
    sizeof(kPn50522Blocks) / sizeof(kPn50522Blocks[0]),
    kPn50522Fields,
    sizeof(kPn50522Fields) / sizeof(kPn50522Fields[0])
  },
  {
    52101,
    "HM-500",
    kPn52101Blocks,
    sizeof(kPn52101Blocks) / sizeof(kPn52101Blocks[0]),
    kPn52101Fields,
    sizeof(kPn52101Fields) / sizeof(kPn52101Fields[0])
  },
  {
    52102,
    "HM-510",
    kPn52102Blocks,
    sizeof(kPn52102Blocks) / sizeof(kPn52102Blocks[0]),
    kPn52102Fields,
    sizeof(kPn52102Fields) / sizeof(kPn52102Fields[0])
  },
  {
    52106,
    "HM-520",
    kPn52106Blocks,
    sizeof(kPn52106Blocks) / sizeof(kPn52106Blocks[0]),
    kPn52106Fields,
    sizeof(kPn52106Fields) / sizeof(kPn52106Fields[0])
  },
  {
    50661,
    "ST-500",
    kPn50661Blocks,
    sizeof(kPn50661Blocks) / sizeof(kPn50661Blocks[0]),
    kPn50661Fields,
    sizeof(kPn50661Fields) / sizeof(kPn50661Fields[0])
  },
  {
    52411,
    "HM-1500",
    kPn52411Blocks,
    sizeof(kPn52411Blocks) / sizeof(kPn52411Blocks[0]),
    kPn52411Fields,
    sizeof(kPn52411Fields) / sizeof(kPn52411Fields[0])
  },
  {
    52217,
    "HM-201",
    kPn52217Blocks,
    sizeof(kPn52217Blocks) / sizeof(kPn52217Blocks[0]),
    kPn52217Fields,
    sizeof(kPn52217Fields) / sizeof(kPn52217Fields[0])
  },
  {
    52111,
    "HM-600",
    kPn52111Blocks,
    sizeof(kPn52111Blocks) / sizeof(kPn52111Blocks[0]),
    kPn52111Fields,
    sizeof(kPn52111Fields) / sizeof(kPn52111Fields[0])
  },
  {
    52112,
    "HM-610",
    kPn52112Blocks,
    sizeof(kPn52112Blocks) / sizeof(kPn52112Blocks[0]),
    kPn52112Fields,
    sizeof(kPn52112Fields) / sizeof(kPn52112Fields[0])
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
    52267,
    "XT-500",
    kPn52267Blocks,
    sizeof(kPn52267Blocks) / sizeof(kPn52267Blocks[0]),
    kPn52267Fields,
    sizeof(kPn52267Fields) / sizeof(kPn52267Fields[0])
  },
  {
    53577,
    "XE-500",
    kPn53577Blocks,
    sizeof(kPn53577Blocks) / sizeof(kPn53577Blocks[0]),
    kPn53577Fields,
    sizeof(kPn53577Fields) / sizeof(kPn53577Fields[0])
  },
  {
    54548,
    "XH-520",
    kPn54548Blocks,
    sizeof(kPn54548Blocks) / sizeof(kPn54548Blocks[0]),
    kPn54548Fields,
    sizeof(kPn54548Fields) / sizeof(kPn54548Fields[0])
  },
  {
    57172,
    "XT-750",
    kPn57172Blocks,
    sizeof(kPn57172Blocks) / sizeof(kPn57172Blocks[0]),
    kPn57172Fields,
    sizeof(kPn57172Fields) / sizeof(kPn57172Fields[0])
  },
  {
    58948,
    "HT-500",
    kPn58948Blocks,
    sizeof(kPn58948Blocks) / sizeof(kPn58948Blocks[0]),
    kPn58948Fields,
    sizeof(kPn58948Fields) / sizeof(kPn58948Fields[0])
  },
};
