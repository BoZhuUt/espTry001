#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "calibration_executor.h"

uint32_t gFakeMillis = 0;

namespace {

int failures = 0;

uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
  }
  return crc;
}

std::vector<uint8_t> withCrc(std::initializer_list<uint8_t> body) {
  std::vector<uint8_t> frame(body);
  uint16_t crc = crc16(frame.data(), frame.size());
  frame.push_back(static_cast<uint8_t>(crc));
  frame.push_back(static_cast<uint8_t>(crc >> 8));
  return frame;
}

void expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    failures++;
  }
}

void expectFrame(const std::vector<uint8_t> &actual,
                 const std::vector<uint8_t> &expected,
                 const std::string &message) {
  if (actual != expected) {
    std::cerr << "FAIL: " << message << "\n  actual: ";
    for (uint8_t value : actual) std::cerr << std::hex << (int)value << ' ';
    std::cerr << "\n  expected: ";
    for (uint8_t value : expected) std::cerr << std::hex << (int)value << ' ';
    std::cerr << std::dec << "\n";
    failures++;
  }
}

CalibrationProfile make57769Profile() {
  CalibrationProfile profile;
  profile.productNum = 57769;
  profile.procedureCount = 2;
  profile.inputCount = 1;
  profile.commandCount = 3;

  profile.procedures[0].firstCommand = 0;
  profile.procedures[0].commandCount = 1;
  profile.commands[0].address1Based = 43006;
  profile.commands[0].type = CalibrationValueType::UInt16;
  profile.commands[0].valueSource = CalibrationValueSource::Fixed;
  profile.commands[0].fixedValue = 21;
  profile.commands[0].waitAfterMs = 1000;

  profile.procedures[1].firstInput = 0;
  profile.procedures[1].inputCount = 1;
  profile.procedures[1].firstCommand = 1;
  profile.procedures[1].commandCount = 2;
  profile.inputs[0].minimum = 1;
  profile.inputs[0].maximum = 1000;
  profile.inputs[0].minimumInclusive = true;
  profile.inputs[0].maximumInclusive = true;
  profile.inputs[0].scaleNumerator = 1;
  profile.inputs[0].scaleDenominator = 1;
  profile.commands[1].address1Based = 44002;
  profile.commands[1].type = CalibrationValueType::Float32CDAB;
  profile.commands[1].valueSource = CalibrationValueSource::Input;
  profile.commands[1].inputIndex = 0;
  profile.commands[1].waitAfterMs = 100;
  profile.commands[2].address1Based = 43006;
  profile.commands[2].type = CalibrationValueType::UInt16;
  profile.commands[2].valueSource = CalibrationValueSource::Fixed;
  profile.commands[2].fixedValue = 22;
  profile.commands[2].waitAfterMs = 1000;
  return profile;
}

CalibrationProfile makeMappedProfile() {
  CalibrationProfile profile;
  profile.productNum = 50661;
  profile.procedureCount = 1;
  profile.commandCount = 1;
  profile.procedures[0].firstCommand = 0;
  profile.procedures[0].commandCount = 1;
  CalibrationCommand &command = profile.commands[0];
  command.address1Based = 1025;
  command.type = CalibrationValueType::UInt16;
  command.valueSource = CalibrationValueSource::Fixed;
  command.fixedValue = 4;
  command.waitAfterMs = 0;
  command.result.enabled = true;
  command.result.address1Based = 1031;
  command.result.pollIntervalMs = 500;
  command.result.timeoutMs = 3000;
  command.result.mappingCount = 3;
  command.result.mappings[0].value = 1;
  command.result.mappings[0].outcome = CalibrationOutcome::Pending;
  std::strcpy(command.result.mappings[0].message, "In progress");
  command.result.mappings[1].value = 0;
  command.result.mappings[1].outcome = CalibrationOutcome::Success;
  std::strcpy(command.result.mappings[1].message, "Calibration succeed.");
  command.result.mappings[2].value = 2048;
  command.result.mappings[2].outcome = CalibrationOutcome::Failure;
  std::strcpy(command.result.mappings[2].message, "Probe fouled");
  return profile;
}

void sendAndAcceptFc06(CalibrationExecutor &executor, HardwareSerial &serial,
                       const std::vector<uint8_t> &expectedRequest) {
  executor.update();
  expectFrame(serial.takeTx(), expectedRequest, "FC06 request bytes");
  serial.inject(expectedRequest);
  executor.update();
}

void testZeroWaitOnly() {
  gFakeMillis = 0;
  HardwareSerial serial;
  CalibrationExecutor executor(serial);
  CalibrationProfile profile = make57769Profile();
  expect(executor.begin(7, profile, 0, nullptr, 0), "zero begin accepted");
  auto request = withCrc({7, 0x06, 0xA7, 0xFD, 0x00, 0x15});
  sendAndAcceptFc06(executor, serial, request);
  expect(executor.state() == CalibrationExecutor::State::WaitingAfterWrite,
         "zero waits after accepted write");
  gFakeMillis = 999;
  executor.update();
  expect(executor.active(), "zero does not finish early");
  gFakeMillis = 1000;
  executor.update();
  expect(executor.state() == CalibrationExecutor::State::Completed,
         "zero completes after final wait");
}

void testSlopeCdabAndSequence() {
  gFakeMillis = 0;
  HardwareSerial serial;
  CalibrationExecutor executor(serial);
  CalibrationProfile profile = make57769Profile();
  float input = 100.0f;
  expect(executor.begin(7, profile, 1, &input, 1), "slope begin accepted");
  executor.update();
  auto fc16 = withCrc({7, 0x10, 0xAB, 0xE1, 0, 2, 4,
                       0x00, 0x00, 0x42, 0xC8});
  expectFrame(serial.takeTx(), fc16, "100.0 float32 uses CDAB order");
  auto fc16Response = withCrc({7, 0x10, 0xAB, 0xE1, 0, 2});
  serial.inject(fc16Response);
  executor.update();
  gFakeMillis = 100;
  executor.update();
  executor.update();
  auto trigger = withCrc({7, 0x06, 0xA7, 0xFD, 0x00, 0x16});
  expectFrame(serial.takeTx(), trigger, "slope trigger follows input write");
  serial.inject(trigger);
  executor.update();
  gFakeMillis = 1100;
  executor.update();
  expect(executor.state() == CalibrationExecutor::State::Completed,
         "slope completes after second wait");
}

void testWriteTimeoutIsUnknown() {
  gFakeMillis = 0;
  HardwareSerial serial;
  CalibrationExecutor executor(serial);
  CalibrationProfile profile = make57769Profile();
  executor.begin(7, profile, 0, nullptr, 0);
  executor.update();
  serial.takeTx();
  gFakeMillis = CalibrationExecutor::kResponseTimeoutMs;
  executor.update();
  expect(executor.state() == CalibrationExecutor::State::ResultUnknown,
         "write response timeout is result unknown");
}

void testBadWriteCrcFailsCommunication() {
  gFakeMillis = 0;
  HardwareSerial serial;
  CalibrationExecutor executor(serial);
  CalibrationProfile profile = make57769Profile();
  executor.begin(7, profile, 0, nullptr, 0);
  executor.update();
  auto response = serial.takeTx();
  response.back() ^= 0xFF;
  serial.inject(response);
  executor.update();
  expect(executor.state() == CalibrationExecutor::State::CommunicationFailure,
         "bad write CRC is communication failure");
}

void testWriteEchoMismatchAndException() {
  {
    gFakeMillis = 0;
    HardwareSerial serial;
    CalibrationExecutor executor(serial);
    CalibrationProfile profile = make57769Profile();
    executor.begin(7, profile, 0, nullptr, 0);
    executor.update();
    serial.takeTx();
    serial.inject(withCrc({7, 0x06, 0xA7, 0xFD, 0, 0x16}));
    executor.update();
    expect(executor.state() == CalibrationExecutor::State::CommunicationFailure,
           "wrong FC06 echoed value is communication failure");
  }
  {
    gFakeMillis = 0;
    HardwareSerial serial;
    CalibrationExecutor executor(serial);
    CalibrationProfile profile = make57769Profile();
    executor.begin(7, profile, 0, nullptr, 0);
    executor.update();
    serial.takeTx();
    serial.inject(withCrc({7, 0x86, 0x02}));
    executor.update();
    expect(executor.state() == CalibrationExecutor::State::DeviceFailure,
           "FC06 exception is device failure");
  }
}

void testInputBoundsRejected() {
  HardwareSerial serial;
  CalibrationExecutor executor(serial);
  CalibrationProfile profile = make57769Profile();
  float tooLow = 0.99f;
  float tooHigh = 1000.01f;
  expect(!executor.begin(7, profile, 1, &tooLow, 1),
         "slope input below minimum rejected");
  expect(!executor.begin(7, profile, 1, &tooHigh, 1),
         "slope input above maximum rejected");
}

void advanceToResultRead(CalibrationExecutor &executor, HardwareSerial &serial) {
  auto write = withCrc({3, 0x06, 0x04, 0x00, 0x00, 0x04});
  sendAndAcceptFc06(executor, serial, write);
  executor.update();
  executor.update();
  auto read = withCrc({3, 0x03, 0x04, 0x06, 0, 1});
  expectFrame(serial.takeTx(), read, "mapped result FC03 request");
}

void testMappedPendingThenSuccess() {
  gFakeMillis = 0;
  HardwareSerial serial;
  CalibrationExecutor executor(serial);
  CalibrationProfile profile = makeMappedProfile();
  executor.begin(3, profile, 0, nullptr, 0);
  advanceToResultRead(executor, serial);
  serial.inject(withCrc({3, 0x03, 2, 0, 1}));
  executor.update();
  expect(executor.state() == CalibrationExecutor::State::WaitingResultPoll,
         "pending result schedules another poll");
  gFakeMillis = 500;
  executor.update();
  serial.takeTx();
  serial.inject(withCrc({3, 0x03, 2, 0, 0}));
  executor.update();
  expect(executor.state() == CalibrationExecutor::State::Completed,
         "mapped success completes");
  expect(std::string(executor.message()) == "Calibration succeed.",
         "mapped success preserves message");
}

void testMappedFailureAndUnknown() {
  {
    gFakeMillis = 0;
    HardwareSerial serial;
    CalibrationExecutor executor(serial);
    CalibrationProfile profile = makeMappedProfile();
    executor.begin(3, profile, 0, nullptr, 0);
    advanceToResultRead(executor, serial);
    serial.inject(withCrc({3, 0x03, 2, 0x08, 0x00}));
    executor.update();
    expect(executor.state() == CalibrationExecutor::State::DeviceFailure,
           "mapped failure is device failure");
  }
  {
    gFakeMillis = 0;
    HardwareSerial serial;
    CalibrationExecutor executor(serial);
    CalibrationProfile profile = makeMappedProfile();
    executor.begin(3, profile, 0, nullptr, 0);
    advanceToResultRead(executor, serial);
    serial.inject(withCrc({3, 0x03, 2, 0x12, 0x34}));
    executor.update();
    expect(executor.state() == CalibrationExecutor::State::ResultUnknown,
           "unmapped result is immediately unknown");
  }
}

void testMappedResultTotalTimeout() {
  gFakeMillis = 0;
  HardwareSerial serial;
  CalibrationExecutor executor(serial);
  CalibrationProfile profile = makeMappedProfile();
  executor.begin(3, profile, 0, nullptr, 0);
  advanceToResultRead(executor, serial);
  serial.inject(withCrc({3, 0x03, 2, 0, 1}));
  executor.update();
  for (uint32_t time = 500; time <= 2500; time += 500) {
    gFakeMillis = time;
    executor.update();
    serial.takeTx();
    serial.inject(withCrc({3, 0x03, 2, 0, 1}));
    executor.update();
  }
  gFakeMillis = 3000;
  executor.update();
  expect(executor.state() == CalibrationExecutor::State::CommunicationFailure,
         "pending result stops at configured total timeout");
}

}  // namespace

int main() {
  testZeroWaitOnly();
  testSlopeCdabAndSequence();
  testWriteTimeoutIsUnknown();
  testBadWriteCrcFailsCommunication();
  testWriteEchoMismatchAndException();
  testInputBoundsRejected();
  testMappedPendingThenSuccess();
  testMappedFailureAndUnknown();
  testMappedResultTotalTimeout();
  if (failures != 0) {
    std::cerr << failures << " calibration executor test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All calibration executor tests passed\n";
  return EXIT_SUCCESS;
}
