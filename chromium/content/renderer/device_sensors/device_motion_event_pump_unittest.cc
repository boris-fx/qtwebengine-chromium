// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/device_sensors/device_motion_event_pump.h"

#include <string.h>

#include <memory>

#include "base/location.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "content/public/test/test_utils.h"
#include "device/sensors/public/cpp/device_motion_hardware_buffer.h"
#include "mojo/public/cpp/system/buffer.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/public/platform/modules/device_orientation/WebDeviceMotionListener.h"

namespace content {

class MockDeviceMotionListener : public blink::WebDeviceMotionListener {
 public:
  MockDeviceMotionListener()
      : did_change_device_motion_(false), number_of_events_(0) {
    memset(&data_, 0, sizeof(data_));
  }
  ~MockDeviceMotionListener() override {}

  void didChangeDeviceMotion(const blink::WebDeviceMotionData& data) override {
    memcpy(&data_, &data, sizeof(data));
    did_change_device_motion_ = true;
    ++number_of_events_;
  }

  bool did_change_device_motion() const {
    return did_change_device_motion_;
  }

  int number_of_events() const { return number_of_events_; }

  const blink::WebDeviceMotionData& data() const {
    return data_;
  }

 private:
  bool did_change_device_motion_;
  int number_of_events_;
  blink::WebDeviceMotionData data_;

  DISALLOW_COPY_AND_ASSIGN(MockDeviceMotionListener);
};

class DeviceMotionEventPumpForTesting : public DeviceMotionEventPump {
 public:
  DeviceMotionEventPumpForTesting()
      : DeviceMotionEventPump(0), stop_on_fire_event_(true) {}
  ~DeviceMotionEventPumpForTesting() override {}

  void set_stop_on_fire_event(bool stop_on_fire_event) {
    stop_on_fire_event_ = stop_on_fire_event;
  }

  bool stop_on_fire_event() { return stop_on_fire_event_; }

  int pump_delay_microseconds() const { return pump_delay_microseconds_; }

  void DidStart(mojo::ScopedSharedBufferHandle renderer_handle) {
    DeviceMotionEventPump::DidStart(std::move(renderer_handle));
  }
  void SendStartMessage() override {}
  void SendStopMessage() override {}
  void FireEvent() override {
    DeviceMotionEventPump::FireEvent();
    if (stop_on_fire_event_) {
      Stop();
      base::MessageLoop::current()->QuitWhenIdle();
    }
  }

 private:
  bool stop_on_fire_event_;

  DISALLOW_COPY_AND_ASSIGN(DeviceMotionEventPumpForTesting);
};

class DeviceMotionEventPumpTest : public testing::Test {
 public:
  DeviceMotionEventPumpTest() = default;

 protected:
  void SetUp() override {
    listener_.reset(new MockDeviceMotionListener);
    motion_pump_.reset(new DeviceMotionEventPumpForTesting);
    shared_memory_ =
        mojo::SharedBufferHandle::Create(sizeof(DeviceMotionHardwareBuffer));
    mapping_ = shared_memory_->Map(sizeof(DeviceMotionHardwareBuffer));
    ASSERT_TRUE(mapping_);
    memset(buffer(), 0, sizeof(DeviceMotionHardwareBuffer));
  }

  void InitBuffer(bool allAvailableSensorsActive) {
    blink::WebDeviceMotionData& data = buffer()->data;
    data.accelerationX = 1;
    data.hasAccelerationX = true;
    data.accelerationY = 2;
    data.hasAccelerationY = true;
    data.accelerationZ = 3;
    data.hasAccelerationZ = true;
    data.allAvailableSensorsAreActive = allAvailableSensorsActive;
  }

  MockDeviceMotionListener* listener() { return listener_.get(); }
  DeviceMotionEventPumpForTesting* motion_pump() { return motion_pump_.get(); }
  mojo::ScopedSharedBufferHandle handle() {
    return shared_memory_->Clone(
        mojo::SharedBufferHandle::AccessMode::READ_ONLY);
  }
  DeviceMotionHardwareBuffer* buffer() {
    return reinterpret_cast<DeviceMotionHardwareBuffer*>(mapping_.get());
  }

 private:
  base::MessageLoop loop_;
  std::unique_ptr<MockDeviceMotionListener> listener_;
  std::unique_ptr<DeviceMotionEventPumpForTesting> motion_pump_;
  mojo::ScopedSharedBufferHandle shared_memory_;
  mojo::ScopedSharedBufferMapping mapping_;

  DISALLOW_COPY_AND_ASSIGN(DeviceMotionEventPumpTest);
};

TEST_F(DeviceMotionEventPumpTest, DidStartPolling) {
  InitBuffer(true);

  motion_pump()->Start(listener());
  motion_pump()->DidStart(handle());

  base::RunLoop().Run();

  const blink::WebDeviceMotionData& received_data = listener()->data();
  EXPECT_TRUE(listener()->did_change_device_motion());
  EXPECT_TRUE(received_data.hasAccelerationX);
  EXPECT_EQ(1, static_cast<double>(received_data.accelerationX));
  EXPECT_TRUE(received_data.hasAccelerationX);
  EXPECT_EQ(2, static_cast<double>(received_data.accelerationY));
  EXPECT_TRUE(received_data.hasAccelerationY);
  EXPECT_EQ(3, static_cast<double>(received_data.accelerationZ));
  EXPECT_TRUE(received_data.hasAccelerationZ);
  EXPECT_FALSE(received_data.hasAccelerationIncludingGravityX);
  EXPECT_FALSE(received_data.hasAccelerationIncludingGravityY);
  EXPECT_FALSE(received_data.hasAccelerationIncludingGravityZ);
  EXPECT_FALSE(received_data.hasRotationRateAlpha);
  EXPECT_FALSE(received_data.hasRotationRateBeta);
  EXPECT_FALSE(received_data.hasRotationRateGamma);
}

TEST_F(DeviceMotionEventPumpTest, DidStartPollingNotAllSensorsActive) {
  InitBuffer(false);

  motion_pump()->Start(listener());
  motion_pump()->DidStart(handle());

  base::RunLoop().Run();

  const blink::WebDeviceMotionData& received_data = listener()->data();
  // No change in device motion because allAvailableSensorsAreActive is false.
  EXPECT_FALSE(listener()->did_change_device_motion());
  EXPECT_FALSE(received_data.hasAccelerationX);
  EXPECT_FALSE(received_data.hasAccelerationX);
  EXPECT_FALSE(received_data.hasAccelerationY);
  EXPECT_FALSE(received_data.hasAccelerationZ);
  EXPECT_FALSE(received_data.hasAccelerationIncludingGravityX);
  EXPECT_FALSE(received_data.hasAccelerationIncludingGravityY);
  EXPECT_FALSE(received_data.hasAccelerationIncludingGravityZ);
  EXPECT_FALSE(received_data.hasRotationRateAlpha);
  EXPECT_FALSE(received_data.hasRotationRateBeta);
  EXPECT_FALSE(received_data.hasRotationRateGamma);
}

// Confirm that the frequency of pumping events is not greater than 60Hz. A rate
// above 60Hz would allow for the detection of keystrokes (crbug.com/421691)
TEST_F(DeviceMotionEventPumpTest, PumpThrottlesEventRate) {
  // Confirm that the delay for pumping events is 60 Hz.
  EXPECT_GE(60, base::Time::kMicrosecondsPerSecond /
      motion_pump()->pump_delay_microseconds());

  InitBuffer(true);

  motion_pump()->set_stop_on_fire_event(false);
  motion_pump()->Start(listener());
  motion_pump()->DidStart(handle());

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::MessageLoop::QuitWhenIdleClosure(),
      base::TimeDelta::FromMilliseconds(100));
  base::RunLoop().Run();
  motion_pump()->Stop();

  // Check that the blink::WebDeviceMotionListener does not receive excess
  // events.
  EXPECT_TRUE(listener()->did_change_device_motion());
  EXPECT_GE(6, listener()->number_of_events());
}

}  // namespace content
