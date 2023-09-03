
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <iostream>
#include <linux/input.h>
#include <linux/joystick.h>
#include <math.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
constexpr int update_interval = 100; // Samples

const char *buttonNames[32] = {
    "TRIGGER", "THUMB", "THUMB2", "TOP",   "TOP2", "PINKIE", "BASE",   "BASE2",
    "BASE3",   "BASE4", "BASE5",  "BASE6", "",     "",       "",       "DEAD",
    "SOUTH",   "EAST",  "C",      "NORTH", "WEST", "Z",      "TL",     "TR",
    "TL2",     "TR2",   "SELECT", "START", "MODE", "THUMBL", "THUMBR", ""};

const char *axisNames[32] = {
    "X",        "Y",        "Z",      "RX",     "RY",         "RZ",
    "THROTTLE", "RUDDER",   "WHEEL",  "GAS",    "BRAKE",      "",
    "",         "",         "",       "",       "HAT0X",      "HAT0Y",
    "HAT1X",    "HAT1Y",    "HAT2X",  "HAT2Y",  "HAT3X",      "HAT3Y",
    "PRESSURE", "DISTANCE", "TILT_X", "TILT_Y", "TOOL_WIDTH", "",
    "",         ""};

struct Axis {
  int min;
  int max;
  float value;
};

struct Joystick {
  static const unsigned int maxButtons = 32;
  static const unsigned int maxAxes = 32;

  bool connected;
  bool buttons[maxButtons];
  Axis axes[maxAxes];
  char name[128];
  int file;
  bool hasRumble;
  short rumbleEffectID;
};

void openJoystick(Joystick &out_joystick, std::string devicePath) {
  int file = open(devicePath.c_str(), O_RDWR);
  if (file != -1) {
    Joystick j = {0};
    j.connected = true;
    j.file = file;

    // Get name
    ioctl(file, EVIOCGNAME(sizeof(j.name)), j.name);

    // Setup axes
    for (unsigned int i = 0; i < Joystick::maxAxes; ++i) {
      input_absinfo axisInfo;
      if (ioctl(file, EVIOCGABS(i), &axisInfo) != -1) {
        j.axes[i].min = axisInfo.minimum;
        j.axes[i].max = axisInfo.maximum;
        std::cout << "Axis " << i << ": " << axisNames[i]
                  << " min: " << axisInfo.minimum
                  << " max: " << axisInfo.maximum << std::endl;
      }
    }

    // Setup rumble
    ff_effect effect = {0};
    effect.type = FF_RUMBLE;
    effect.id = -1;
    if (ioctl(file, EVIOCSFF, &effect) != -1) {
      j.rumbleEffectID = effect.id;
      j.hasRumble = true;
    }

    out_joystick = j;
  } else {
    throw std::runtime_error(std::format("Failed to open joystick at {}: {}",
                                         devicePath, strerror(errno)));
  }
}

void closeJoystick(Joystick &joystick) {
  if (joystick.connected) {
    close(joystick.file);
    joystick.connected = false;
  }
}

void readJoystickInput(Joystick *joystick) {
  input_event event;
  if (read(joystick->file, &event, sizeof(event)) > 0) {
    if (event.type == EV_KEY && event.code >= BTN_JOYSTICK &&
        event.code <= BTN_THUMBR) {
      joystick->buttons[event.code - 0x120] = event.value;
    }
    if (event.type == EV_ABS && event.code < ABS_TOOL_WIDTH) {
      Axis *axis = &joystick->axes[event.code];
      float normalized =
          (event.value - axis->min) / (float)(axis->max - axis->min) * 2 - 1;
      joystick->axes[event.code].value = normalized;
    }
  } else {
    throw std::runtime_error("Failed to read joystick input");
  }
}

void setJoystickRumble(Joystick joystick, short weakRumble,
                       short strongRumble) {
  if (joystick.hasRumble) {
    ff_effect effect = {0};
    effect.type = FF_RUMBLE;
    effect.id = joystick.rumbleEffectID;
    effect.replay.length = 5000;
    effect.replay.delay = 0;
    effect.u.rumble.weak_magnitude = weakRumble;
    effect.u.rumble.strong_magnitude = strongRumble;
    ioctl(joystick.file, EVIOCSFF, &effect);

    input_event play = {0};
    play.type = EV_FF;
    play.code = joystick.rumbleEffectID;
    play.value = 1;
    write(joystick.file, &play, sizeof(play));
  }
}

bool isSameSample(float now, float last) {
  constexpr float eps = 0.0001f;
  return fabs(now - last) < eps;
}

int main(const int argc, const char **argv) {
  int maxSamples = 0;
  if (argc < 2) {
    std::cout << std::format(
                     "Usage: {} <joystick event device path> [max samples]",
                     argv[0])
              << std::endl;
    return 1;
  }
  if (argc > 2) {
    try {
      maxSamples = std::stoi(argv[2]);
    } catch (std::exception &e) {
      std::cout << "Failed to parse max samples: " << e.what() << std::endl;
      return 1;
    }
  }
  Joystick js;

  openJoystick(js, argv[1]);
  for (int i = 0; i < 100; ++i) {
    readJoystickInput(&js); // Warmup
  }

  float lastX = 0, lastY = 0;
  auto lastTime = std::chrono::high_resolution_clock::now();
  auto startTime = lastTime;
  int sampleCount = 0, totalSampleCount = 0;
  int effectiveSampleCount = 0, totalEffectiveSampleCount = 0;
  while (totalSampleCount < maxSamples || maxSamples == 0) {
    readJoystickInput(&js);
    sampleCount++;
    totalSampleCount++;

    if (!isSameSample(js.axes[0].value, lastX) &&
        !isSameSample(js.axes[1].value, lastY)) {
      effectiveSampleCount++;
      totalEffectiveSampleCount++;
      lastX = js.axes[0].value;
      lastY = js.axes[1].value;
    }

    if (sampleCount % update_interval == 0) {
      auto now = std::chrono::high_resolution_clock::now();
      auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime)
              .count();
      float effectiveRate = effectiveSampleCount / (elapsed / 1000.0f);
      float rate = sampleCount / (elapsed / 1000.0f);
      std::cout << std::format("Rate: {:.1f}Hz, Effective rate: {:.1f}Hz", rate,
                               effectiveRate)
                << std::endl;
      lastTime = now;
      sampleCount = 0;
      effectiveSampleCount = 0;
    }
  }
  auto endTime = std::chrono::high_resolution_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime)
          .count();
  float effectiveRate = totalEffectiveSampleCount / (elapsed / 1000.0f);
  float rate = totalSampleCount / (elapsed / 1000.0f);
  std::cout << std::format(
                   "Total rate: {:.1f}Hz, Total effective rate: {:.1f}Hz", rate,
                   effectiveRate)
            << std::endl;
  std::cout << "Done" << std::endl;
  closeJoystick(js);
}