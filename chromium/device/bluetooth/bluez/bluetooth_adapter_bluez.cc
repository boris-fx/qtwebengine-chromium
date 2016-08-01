// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluez/bluetooth_adapter_bluez.h"

#include <memory>
#include <set>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/sequenced_task_runner.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "device/bluetooth/bluetooth_common.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_discovery_session_outcome.h"
#include "device/bluetooth/bluetooth_socket_thread.h"
#include "device/bluetooth/bluetooth_uuid.h"
#include "device/bluetooth/bluez/bluetooth_adapter_profile_bluez.h"
#include "device/bluetooth/bluez/bluetooth_advertisement_bluez.h"
#include "device/bluetooth/bluez/bluetooth_audio_sink_bluez.h"
#include "device/bluetooth/bluez/bluetooth_device_bluez.h"
#include "device/bluetooth/bluez/bluetooth_gatt_service_bluez.h"
#include "device/bluetooth/bluez/bluetooth_local_gatt_characteristic_bluez.h"
#include "device/bluetooth/bluez/bluetooth_local_gatt_service_bluez.h"
#include "device/bluetooth/bluez/bluetooth_pairing_bluez.h"
#include "device/bluetooth/bluez/bluetooth_socket_bluez.h"
#include "device/bluetooth/dbus/bluetooth_adapter_client.h"
#include "device/bluetooth/dbus/bluetooth_agent_manager_client.h"
#include "device/bluetooth/dbus/bluetooth_agent_service_provider.h"
#include "device/bluetooth/dbus/bluetooth_device_client.h"
#include "device/bluetooth/dbus/bluetooth_gatt_application_service_provider.h"
#include "device/bluetooth/dbus/bluetooth_gatt_manager_client.h"
#include "device/bluetooth/dbus/bluetooth_input_client.h"
#include "device/bluetooth/dbus/bluez_dbus_manager.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

#if defined(OS_CHROMEOS)
#include "chromeos/system/devicetype.h"
#endif

using device::BluetoothAdapter;
using device::BluetoothAudioSink;
using device::BluetoothDevice;
using device::BluetoothDiscoveryFilter;
using device::BluetoothSocket;
using device::BluetoothUUID;
using device::UMABluetoothDiscoverySessionOutcome;

namespace {

// The agent path is relatively meaningless since BlueZ only permits one to
// exist per D-Bus connection, it just has to be unique within Chromium.
const char kAgentPath[] = "/org/chromium/bluetooth_agent";
const char kGattApplicationObjectPath[] = "/gatt_application";

void OnUnregisterAgentError(const std::string& error_name,
                            const std::string& error_message) {
  // It's okay if the agent didn't exist, it means we never saw an adapter.
  if (error_name == bluetooth_agent_manager::kErrorDoesNotExist)
    return;

  LOG(WARNING) << "Failed to unregister pairing agent: " << error_name << ": "
               << error_message;
}

UMABluetoothDiscoverySessionOutcome TranslateDiscoveryErrorToUMA(
    const std::string& error_name) {
  if (error_name == bluez::BluetoothAdapterClient::kUnknownAdapterError) {
    return UMABluetoothDiscoverySessionOutcome::BLUEZ_DBUS_UNKNOWN_ADAPTER;
  } else if (error_name == bluez::BluetoothAdapterClient::kNoResponseError) {
    return UMABluetoothDiscoverySessionOutcome::BLUEZ_DBUS_NO_RESPONSE;
  } else if (error_name == bluetooth_device::kErrorInProgress) {
    return UMABluetoothDiscoverySessionOutcome::BLUEZ_DBUS_IN_PROGRESS;
  } else if (error_name == bluetooth_device::kErrorNotReady) {
    return UMABluetoothDiscoverySessionOutcome::BLUEZ_DBUS_NOT_READY;
  } else if (error_name == bluetooth_device::kErrorNotSupported) {
    return UMABluetoothDiscoverySessionOutcome::BLUEZ_DBUS_UNSUPPORTED_DEVICE;
  } else if (error_name == bluetooth_device::kErrorFailed) {
    return UMABluetoothDiscoverySessionOutcome::FAILED;
  } else {
    LOG(WARNING) << "Can't histogram DBus error " << error_name;
    return UMABluetoothDiscoverySessionOutcome::UNKNOWN;
  }
}

}  // namespace

namespace device {

// static
base::WeakPtr<BluetoothAdapter> BluetoothAdapter::CreateAdapter(
    const InitCallback& init_callback) {
  return bluez::BluetoothAdapterBlueZ::CreateAdapter(init_callback);
}

}  // namespace device

namespace bluez {

namespace {

void OnRegisterationErrorCallback(
    const device::BluetoothGattService::ErrorCallback& error_callback,
    bool is_register_callback,
    const std::string& error_name,
    const std::string& error_message) {
  if (is_register_callback) {
    VLOG(1) << "Failed to Register service: " << error_name << ", "
            << error_message;
  } else {
    VLOG(1) << "Failed to Unregister service: " << error_name << ", "
            << error_message;
  }
  error_callback.Run(
      BluetoothGattServiceBlueZ::DBusErrorToServiceError(error_name));
}

void DoNothingOnError(
    device::BluetoothGattService::GattErrorCode /*error_code*/) {}

}  // namespace

// static
base::WeakPtr<BluetoothAdapter> BluetoothAdapterBlueZ::CreateAdapter(
    const InitCallback& init_callback) {
  BluetoothAdapterBlueZ* adapter = new BluetoothAdapterBlueZ(init_callback);
  return adapter->weak_ptr_factory_.GetWeakPtr();
}

void BluetoothAdapterBlueZ::Shutdown() {
  if (dbus_is_shutdown_)
    return;
  DCHECK(bluez::BluezDBusManager::IsInitialized())
      << "Call BluetoothAdapterFactory::Shutdown() before "
         "BluezDBusManager::Shutdown().";

  // Since we don't initialize anything if Object Manager is not supported,
  // no need to do any clean up.
  if (!bluez::BluezDBusManager::Get()->IsObjectManagerSupported()) {
    dbus_is_shutdown_ = true;
    return;
  }

  if (IsPresent())
    RemoveAdapter();  // Also deletes devices_.
  DCHECK(devices_.empty());

  // profiles_ must be empty because all BluetoothSockets have been notified
  // that this adapter is disappearing.
  DCHECK(profiles_.empty());

  // Some profiles may have been released but not yet removed; it is safe to
  // delete them.
  for (auto& it : released_profiles_)
    delete it.second;
  released_profiles_.clear();

  for (auto& it : profile_queues_)
    delete it.second;
  profile_queues_.clear();

  bluez::BluezDBusManager::Get()->GetBluetoothAdapterClient()->RemoveObserver(
      this);
  bluez::BluezDBusManager::Get()->GetBluetoothDeviceClient()->RemoveObserver(
      this);
  bluez::BluezDBusManager::Get()->GetBluetoothInputClient()->RemoveObserver(
      this);

  VLOG(1) << "Unregistering pairing agent";
  bluez::BluezDBusManager::Get()
      ->GetBluetoothAgentManagerClient()
      ->UnregisterAgent(dbus::ObjectPath(kAgentPath),
                        base::Bind(&base::DoNothing),
                        base::Bind(&OnUnregisterAgentError));

  agent_.reset();

  dbus_is_shutdown_ = true;
}

BluetoothAdapterBlueZ::BluetoothAdapterBlueZ(const InitCallback& init_callback)
    : init_callback_(init_callback),
      initialized_(false),
      dbus_is_shutdown_(false),
      num_discovery_sessions_(0),
      discovery_request_pending_(false),
      weak_ptr_factory_(this) {
  ui_task_runner_ = base::ThreadTaskRunnerHandle::Get();
  socket_thread_ = device::BluetoothSocketThread::Get();

  // Can't initialize the adapter until DBus clients are ready.
  if (bluez::BluezDBusManager::Get()->IsObjectManagerSupportKnown()) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&BluetoothAdapterBlueZ::Init,
                              weak_ptr_factory_.GetWeakPtr()));
    return;
  }
  bluez::BluezDBusManager::Get()->CallWhenObjectManagerSupportIsKnown(
      base::Bind(&BluetoothAdapterBlueZ::Init, weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothAdapterBlueZ::Init() {
  // If the platform doesn't support Object Manager then Bluez 5 is probably
  // not present. In this case we just return without initializing anything.
  if (!bluez::BluezDBusManager::Get()->IsObjectManagerSupported()) {
    initialized_ = true;
    init_callback_.Run();
    return;
  }

  bluez::BluezDBusManager::Get()->GetBluetoothAdapterClient()->AddObserver(
      this);
  bluez::BluezDBusManager::Get()->GetBluetoothDeviceClient()->AddObserver(this);
  bluez::BluezDBusManager::Get()->GetBluetoothInputClient()->AddObserver(this);

  // Register the pairing agent.
  dbus::Bus* system_bus = bluez::BluezDBusManager::Get()->GetSystemBus();
  agent_.reset(bluez::BluetoothAgentServiceProvider::Create(
      system_bus, dbus::ObjectPath(kAgentPath), this));
  DCHECK(agent_.get());

  std::vector<dbus::ObjectPath> object_paths = bluez::BluezDBusManager::Get()
                                                   ->GetBluetoothAdapterClient()
                                                   ->GetAdapters();

  if (!object_paths.empty()) {
    VLOG(1) << object_paths.size() << " Bluetooth adapter(s) available.";
    SetAdapter(object_paths[0]);
  }
  initialized_ = true;
  init_callback_.Run();
}

BluetoothAdapterBlueZ::~BluetoothAdapterBlueZ() {
  Shutdown();
}

std::string BluetoothAdapterBlueZ::GetAddress() const {
  if (!IsPresent())
    return std::string();

  bluez::BluetoothAdapterClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothAdapterClient()
          ->GetProperties(object_path_);
  DCHECK(properties);

  return BluetoothDevice::CanonicalizeAddress(properties->address.value());
}

std::string BluetoothAdapterBlueZ::GetName() const {
  if (!IsPresent())
    return std::string();

  bluez::BluetoothAdapterClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothAdapterClient()
          ->GetProperties(object_path_);
  DCHECK(properties);

  return properties->alias.value();
}

void BluetoothAdapterBlueZ::SetName(const std::string& name,
                                    const base::Closure& callback,
                                    const ErrorCallback& error_callback) {
  if (!IsPresent()) {
    error_callback.Run();
    return;
  }

  bluez::BluezDBusManager::Get()
      ->GetBluetoothAdapterClient()
      ->GetProperties(object_path_)
      ->alias.Set(
          name,
          base::Bind(&BluetoothAdapterBlueZ::OnPropertyChangeCompleted,
                     weak_ptr_factory_.GetWeakPtr(), callback, error_callback));
}

bool BluetoothAdapterBlueZ::IsInitialized() const {
  return initialized_;
}

bool BluetoothAdapterBlueZ::IsPresent() const {
  return !dbus_is_shutdown_ && !object_path_.value().empty();
}

bool BluetoothAdapterBlueZ::IsPowered() const {
  if (!IsPresent())
    return false;

  bluez::BluetoothAdapterClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothAdapterClient()
          ->GetProperties(object_path_);

  return properties->powered.value();
}

void BluetoothAdapterBlueZ::SetPowered(bool powered,
                                       const base::Closure& callback,
                                       const ErrorCallback& error_callback) {
  if (!IsPresent()) {
    error_callback.Run();
    return;
  }

  bluez::BluezDBusManager::Get()
      ->GetBluetoothAdapterClient()
      ->GetProperties(object_path_)
      ->powered.Set(
          powered,
          base::Bind(&BluetoothAdapterBlueZ::OnPropertyChangeCompleted,
                     weak_ptr_factory_.GetWeakPtr(), callback, error_callback));
}

bool BluetoothAdapterBlueZ::IsDiscoverable() const {
  if (!IsPresent())
    return false;

  bluez::BluetoothAdapterClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothAdapterClient()
          ->GetProperties(object_path_);

  return properties->discoverable.value();
}

void BluetoothAdapterBlueZ::SetDiscoverable(
    bool discoverable,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  if (!IsPresent()) {
    error_callback.Run();
    return;
  }

  bluez::BluezDBusManager::Get()
      ->GetBluetoothAdapterClient()
      ->GetProperties(object_path_)
      ->discoverable.Set(
          discoverable,
          base::Bind(&BluetoothAdapterBlueZ::OnSetDiscoverable,
                     weak_ptr_factory_.GetWeakPtr(), callback, error_callback));
}

uint32_t BluetoothAdapterBlueZ::GetDiscoverableTimeout() const {
  if (!IsPresent())
    return 0;

  bluez::BluetoothAdapterClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothAdapterClient()
          ->GetProperties(object_path_);

  return properties->discoverable_timeout.value();
}

bool BluetoothAdapterBlueZ::IsDiscovering() const {
  if (!IsPresent())
    return false;

  bluez::BluetoothAdapterClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothAdapterClient()
          ->GetProperties(object_path_);

  return properties->discovering.value();
}

BluetoothAdapterBlueZ::UUIDList BluetoothAdapterBlueZ::GetUUIDs() const {
  bluez::BluetoothAdapterClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothAdapterClient()
          ->GetProperties(object_path_);
  DCHECK(properties);

  std::vector<std::string> uuids = properties->uuids.value();

  return UUIDList(uuids.begin(), uuids.end());
}

void BluetoothAdapterBlueZ::CreateRfcommService(
    const BluetoothUUID& uuid,
    const ServiceOptions& options,
    const CreateServiceCallback& callback,
    const CreateServiceErrorCallback& error_callback) {
  DCHECK(!dbus_is_shutdown_);
  VLOG(1) << object_path_.value()
          << ": Creating RFCOMM service: " << uuid.canonical_value();
  scoped_refptr<BluetoothSocketBlueZ> socket =
      BluetoothSocketBlueZ::CreateBluetoothSocket(ui_task_runner_,
                                                  socket_thread_);
  socket->Listen(this, BluetoothSocketBlueZ::kRfcomm, uuid, options,
                 base::Bind(callback, socket), error_callback);
}

void BluetoothAdapterBlueZ::CreateL2capService(
    const BluetoothUUID& uuid,
    const ServiceOptions& options,
    const CreateServiceCallback& callback,
    const CreateServiceErrorCallback& error_callback) {
  DCHECK(!dbus_is_shutdown_);
  VLOG(1) << object_path_.value()
          << ": Creating L2CAP service: " << uuid.canonical_value();
  scoped_refptr<BluetoothSocketBlueZ> socket =
      BluetoothSocketBlueZ::CreateBluetoothSocket(ui_task_runner_,
                                                  socket_thread_);
  socket->Listen(this, BluetoothSocketBlueZ::kL2cap, uuid, options,
                 base::Bind(callback, socket), error_callback);
}

void BluetoothAdapterBlueZ::RegisterAudioSink(
    const BluetoothAudioSink::Options& options,
    const device::BluetoothAdapter::AcquiredCallback& callback,
    const BluetoothAudioSink::ErrorCallback& error_callback) {
  VLOG(1) << "Registering audio sink";
  if (!this->IsPresent()) {
    error_callback.Run(BluetoothAudioSink::ERROR_INVALID_ADAPTER);
    return;
  }
  scoped_refptr<BluetoothAudioSinkBlueZ> audio_sink(
      new BluetoothAudioSinkBlueZ(this));
  audio_sink->Register(options,
                       base::Bind(&BluetoothAdapterBlueZ::OnRegisterAudioSink,
                                  weak_ptr_factory_.GetWeakPtr(), callback,
                                  error_callback, audio_sink),
                       error_callback);
}

void BluetoothAdapterBlueZ::RegisterAdvertisement(
    std::unique_ptr<device::BluetoothAdvertisement::Data> advertisement_data,
    const CreateAdvertisementCallback& callback,
    const CreateAdvertisementErrorCallback& error_callback) {
  scoped_refptr<BluetoothAdvertisementBlueZ> advertisement(
      new BluetoothAdvertisementBlueZ(std::move(advertisement_data), this));
  advertisement->Register(base::Bind(callback, advertisement), error_callback);
}

device::BluetoothLocalGattService* BluetoothAdapterBlueZ::GetGattService(
    const std::string& identifier) const {
  const auto& service = owned_gatt_services_.find(dbus::ObjectPath(identifier));
  return service == owned_gatt_services_.end() ? nullptr
                                               : service->second.get();
}

void BluetoothAdapterBlueZ::RemovePairingDelegateInternal(
    BluetoothDevice::PairingDelegate* pairing_delegate) {
  // Check if any device is using the pairing delegate.
  // If so, clear the pairing context which will make any responses no-ops.
  for (DevicesMap::const_iterator iter = devices_.begin();
       iter != devices_.end(); ++iter) {
    BluetoothDeviceBlueZ* device_bluez =
        static_cast<BluetoothDeviceBlueZ*>(iter->second);

    BluetoothPairingBlueZ* pairing = device_bluez->GetPairing();
    if (pairing && pairing->GetPairingDelegate() == pairing_delegate)
      device_bluez->EndPairing();
  }
}

void BluetoothAdapterBlueZ::AdapterAdded(const dbus::ObjectPath& object_path) {
  // Set the adapter to the newly added adapter only if no adapter is present.
  if (!IsPresent())
    SetAdapter(object_path);
}

void BluetoothAdapterBlueZ::AdapterRemoved(
    const dbus::ObjectPath& object_path) {
  if (object_path == object_path_)
    RemoveAdapter();
}

void BluetoothAdapterBlueZ::AdapterPropertyChanged(
    const dbus::ObjectPath& object_path,
    const std::string& property_name) {
  if (object_path != object_path_)
    return;
  DCHECK(IsPresent());

  bluez::BluetoothAdapterClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothAdapterClient()
          ->GetProperties(object_path_);

  if (property_name == properties->powered.name()) {
    NotifyAdapterPoweredChanged(properties->powered.value());
  } else if (property_name == properties->discoverable.name()) {
    DiscoverableChanged(properties->discoverable.value());
  } else if (property_name == properties->discovering.name()) {
    DiscoveringChanged(properties->discovering.value());
  }
}

void BluetoothAdapterBlueZ::DeviceAdded(const dbus::ObjectPath& object_path) {
  DCHECK(bluez::BluezDBusManager::Get());
  bluez::BluetoothDeviceClient::Properties* properties =
      bluez::BluezDBusManager::Get()->GetBluetoothDeviceClient()->GetProperties(
          object_path);
  if (!properties || properties->adapter.value() != object_path_)
    return;
  DCHECK(IsPresent());

  BluetoothDeviceBlueZ* device_bluez = new BluetoothDeviceBlueZ(
      this, object_path, ui_task_runner_, socket_thread_);
  DCHECK(devices_.find(device_bluez->GetAddress()) == devices_.end());

  devices_.set(device_bluez->GetAddress(),
               std::unique_ptr<BluetoothDevice>(device_bluez));

  FOR_EACH_OBSERVER(BluetoothAdapter::Observer, observers_,
                    DeviceAdded(this, device_bluez));
}

void BluetoothAdapterBlueZ::DeviceRemoved(const dbus::ObjectPath& object_path) {
  for (DevicesMap::const_iterator iter = devices_.begin();
       iter != devices_.end(); ++iter) {
    BluetoothDeviceBlueZ* device_bluez =
        static_cast<BluetoothDeviceBlueZ*>(iter->second);
    if (device_bluez->object_path() == object_path) {
      std::unique_ptr<BluetoothDevice> scoped_device =
          devices_.take_and_erase(iter->first);

      FOR_EACH_OBSERVER(BluetoothAdapter::Observer, observers_,
                        DeviceRemoved(this, device_bluez));
      return;
    }
  }
}

void BluetoothAdapterBlueZ::DevicePropertyChanged(
    const dbus::ObjectPath& object_path,
    const std::string& property_name) {
  BluetoothDeviceBlueZ* device_bluez = GetDeviceWithPath(object_path);
  if (!device_bluez)
    return;

  bluez::BluetoothDeviceClient::Properties* properties =
      bluez::BluezDBusManager::Get()->GetBluetoothDeviceClient()->GetProperties(
          object_path);

  if (property_name == properties->address.name()) {
    for (DevicesMap::iterator iter = devices_.begin(); iter != devices_.end();
         ++iter) {
      if (iter->second->GetAddress() == device_bluez->GetAddress()) {
        std::string old_address = iter->first;
        VLOG(1) << "Device changed address, old: " << old_address
                << " new: " << device_bluez->GetAddress();
        std::unique_ptr<BluetoothDevice> scoped_device =
            devices_.take_and_erase(iter);
        ignore_result(scoped_device.release());

        DCHECK(devices_.find(device_bluez->GetAddress()) == devices_.end());
        devices_.set(device_bluez->GetAddress(),
                     std::unique_ptr<BluetoothDevice>(device_bluez));
        NotifyDeviceAddressChanged(device_bluez, old_address);
        break;
      }
    }
  }

  if (property_name == properties->bluetooth_class.name() ||
      property_name == properties->appearance.name() ||
      property_name == properties->address.name() ||
      property_name == properties->alias.name() ||
      property_name == properties->paired.name() ||
      property_name == properties->trusted.name() ||
      property_name == properties->connected.name() ||
      property_name == properties->uuids.name() ||
      property_name == properties->rssi.name() ||
      property_name == properties->tx_power.name()) {
    NotifyDeviceChanged(device_bluez);
  }

  if (property_name == properties->services_resolved.name() &&
      properties->services_resolved.value()) {
    NotifyGattServicesDiscovered(device_bluez);
  }

  // When a device becomes paired, mark it as trusted so that the user does
  // not need to approve every incoming connection
  if (property_name == properties->paired.name()) {
    if (properties->paired.value() && !properties->trusted.value()) {
      device_bluez->SetTrusted();
    }
    NotifyDevicePairedChanged(device_bluez, properties->paired.value());
  }

  // UMA connection counting
  if (property_name == properties->connected.name()) {
    // PlayStation joystick tries to reconnect after disconnection from USB.
    // If it is still not trusted, set it, so it becomes available on the
    // list of known devices.
    if (properties->connected.value()) {
      if (device_bluez->IsTrustable() && !properties->trusted.value())
        device_bluez->SetTrusted();
    }

    int count = 0;

    for (DevicesMap::const_iterator iter = devices_.begin();
         iter != devices_.end(); ++iter) {
      if (iter->second->IsPaired() && iter->second->IsConnected())
        ++count;
    }

    UMA_HISTOGRAM_COUNTS_100("Bluetooth.ConnectedDeviceCount", count);
  }
}

void BluetoothAdapterBlueZ::InputPropertyChanged(
    const dbus::ObjectPath& object_path,
    const std::string& property_name) {
  BluetoothDeviceBlueZ* device_bluez = GetDeviceWithPath(object_path);
  if (!device_bluez)
    return;

  bluez::BluetoothInputClient::Properties* properties =
      bluez::BluezDBusManager::Get()->GetBluetoothInputClient()->GetProperties(
          object_path);

  // Properties structure can be removed, which triggers a change in the
  // BluetoothDevice::IsConnectable() property, as does a change in the
  // actual reconnect_mode property.
  if (!properties || property_name == properties->reconnect_mode.name()) {
    NotifyDeviceChanged(device_bluez);
  }
}

void BluetoothAdapterBlueZ::Released() {
  VLOG(1) << "Release";
  if (!IsPresent())
    return;
  DCHECK(agent_.get());

  // Called after we unregister the pairing agent, e.g. when changing I/O
  // capabilities. Nothing much to be done right now.
}

void BluetoothAdapterBlueZ::RequestPinCode(const dbus::ObjectPath& device_path,
                                           const PinCodeCallback& callback) {
  DCHECK(IsPresent());
  DCHECK(agent_.get());
  VLOG(1) << device_path.value() << ": RequestPinCode";

  BluetoothPairingBlueZ* pairing = GetPairing(device_path);
  if (!pairing) {
    callback.Run(REJECTED, "");
    return;
  }

  pairing->RequestPinCode(callback);
}

void BluetoothAdapterBlueZ::DisplayPinCode(const dbus::ObjectPath& device_path,
                                           const std::string& pincode) {
  DCHECK(IsPresent());
  DCHECK(agent_.get());
  VLOG(1) << device_path.value() << ": DisplayPinCode: " << pincode;

  BluetoothPairingBlueZ* pairing = GetPairing(device_path);
  if (!pairing)
    return;

  pairing->DisplayPinCode(pincode);
}

void BluetoothAdapterBlueZ::RequestPasskey(const dbus::ObjectPath& device_path,
                                           const PasskeyCallback& callback) {
  DCHECK(IsPresent());
  DCHECK(agent_.get());
  VLOG(1) << device_path.value() << ": RequestPasskey";

  BluetoothPairingBlueZ* pairing = GetPairing(device_path);
  if (!pairing) {
    callback.Run(REJECTED, 0);
    return;
  }

  pairing->RequestPasskey(callback);
}

void BluetoothAdapterBlueZ::DisplayPasskey(const dbus::ObjectPath& device_path,
                                           uint32_t passkey,
                                           uint16_t entered) {
  DCHECK(IsPresent());
  DCHECK(agent_.get());
  VLOG(1) << device_path.value() << ": DisplayPasskey: " << passkey << " ("
          << entered << " entered)";

  BluetoothPairingBlueZ* pairing = GetPairing(device_path);
  if (!pairing)
    return;

  if (entered == 0)
    pairing->DisplayPasskey(passkey);

  pairing->KeysEntered(entered);
}

void BluetoothAdapterBlueZ::RequestConfirmation(
    const dbus::ObjectPath& device_path,
    uint32_t passkey,
    const ConfirmationCallback& callback) {
  DCHECK(IsPresent());
  DCHECK(agent_.get());
  VLOG(1) << device_path.value() << ": RequestConfirmation: " << passkey;

  BluetoothPairingBlueZ* pairing = GetPairing(device_path);
  if (!pairing) {
    callback.Run(REJECTED);
    return;
  }

  pairing->RequestConfirmation(passkey, callback);
}

void BluetoothAdapterBlueZ::RequestAuthorization(
    const dbus::ObjectPath& device_path,
    const ConfirmationCallback& callback) {
  DCHECK(IsPresent());
  DCHECK(agent_.get());
  VLOG(1) << device_path.value() << ": RequestAuthorization";

  BluetoothPairingBlueZ* pairing = GetPairing(device_path);
  if (!pairing) {
    callback.Run(REJECTED);
    return;
  }

  pairing->RequestAuthorization(callback);
}

void BluetoothAdapterBlueZ::AuthorizeService(
    const dbus::ObjectPath& device_path,
    const std::string& uuid,
    const ConfirmationCallback& callback) {
  DCHECK(IsPresent());
  DCHECK(agent_.get());
  VLOG(1) << device_path.value() << ": AuthorizeService: " << uuid;

  BluetoothDeviceBlueZ* device_bluez = GetDeviceWithPath(device_path);
  if (!device_bluez) {
    callback.Run(CANCELLED);
    return;
  }

  // We always set paired devices to Trusted, so the only reason that this
  // method call would ever be called is in the case of a race condition where
  // our "Set('Trusted', true)" method call is still pending in the Bluetooth
  // daemon because it's busy handling the incoming connection.
  if (device_bluez->IsPaired()) {
    callback.Run(SUCCESS);
    return;
  }

  // TODO(keybuk): reject service authorizations when not paired, determine
  // whether this is acceptable long-term.
  LOG(WARNING) << "Rejecting service connection from unpaired device "
               << device_bluez->GetAddress() << " for UUID " << uuid;
  callback.Run(REJECTED);
}

void BluetoothAdapterBlueZ::Cancel() {
  DCHECK(IsPresent());
  DCHECK(agent_.get());
  VLOG(1) << "Cancel";
}

void BluetoothAdapterBlueZ::OnRegisterAgent() {
  VLOG(1) << "Pairing agent registered, requesting to be made default";

  bluez::BluezDBusManager::Get()
      ->GetBluetoothAgentManagerClient()
      ->RequestDefaultAgent(
          dbus::ObjectPath(kAgentPath),
          base::Bind(&BluetoothAdapterBlueZ::OnRequestDefaultAgent,
                     weak_ptr_factory_.GetWeakPtr()),
          base::Bind(&BluetoothAdapterBlueZ::OnRequestDefaultAgentError,
                     weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothAdapterBlueZ::OnRegisterAgentError(
    const std::string& error_name,
    const std::string& error_message) {
  // Our agent being already registered isn't an error.
  if (error_name == bluetooth_agent_manager::kErrorAlreadyExists)
    return;

  LOG(WARNING) << ": Failed to register pairing agent: " << error_name << ": "
               << error_message;
}

void BluetoothAdapterBlueZ::OnRequestDefaultAgent() {
  VLOG(1) << "Pairing agent now default";
}

void BluetoothAdapterBlueZ::OnRequestDefaultAgentError(
    const std::string& error_name,
    const std::string& error_message) {
  LOG(WARNING) << ": Failed to make pairing agent default: " << error_name
               << ": " << error_message;
}

void BluetoothAdapterBlueZ::OnRegisterAudioSink(
    const device::BluetoothAdapter::AcquiredCallback& callback,
    const device::BluetoothAudioSink::ErrorCallback& error_callback,
    scoped_refptr<BluetoothAudioSink> audio_sink) {
  if (!IsPresent()) {
    VLOG(1) << "Failed to register audio sink, adapter not present";
    error_callback.Run(BluetoothAudioSink::ERROR_INVALID_ADAPTER);
    return;
  }
  DCHECK(audio_sink.get());
  callback.Run(audio_sink);
}

void BluetoothAdapterBlueZ::CreateServiceRecord(
    const BluetoothServiceRecordBlueZ& record,
    const ServiceRecordCallback& callback,
    const ServiceRecordErrorCallback& error_callback) {
  bluez::BluezDBusManager::Get()
      ->GetBluetoothAdapterClient()
      ->CreateServiceRecord(
          object_path_, record, callback,
          base::Bind(&BluetoothAdapterBlueZ::ServiceRecordErrorConnector,
                     weak_ptr_factory_.GetWeakPtr(), error_callback));
}

void BluetoothAdapterBlueZ::RemoveServiceRecord(
    uint32_t handle,
    const base::Closure& callback,
    const ServiceRecordErrorCallback& error_callback) {
  bluez::BluezDBusManager::Get()
      ->GetBluetoothAdapterClient()
      ->RemoveServiceRecord(
          object_path_, handle, callback,
          base::Bind(&BluetoothAdapterBlueZ::ServiceRecordErrorConnector,
                     weak_ptr_factory_.GetWeakPtr(), error_callback));
}

BluetoothDeviceBlueZ* BluetoothAdapterBlueZ::GetDeviceWithPath(
    const dbus::ObjectPath& object_path) {
  if (!IsPresent())
    return nullptr;

  for (DevicesMap::const_iterator iter = devices_.begin();
       iter != devices_.end(); ++iter) {
    BluetoothDeviceBlueZ* device_bluez =
        static_cast<BluetoothDeviceBlueZ*>(iter->second);
    if (device_bluez->object_path() == object_path)
      return device_bluez;
  }

  return nullptr;
}

BluetoothPairingBlueZ* BluetoothAdapterBlueZ::GetPairing(
    const dbus::ObjectPath& object_path) {
  DCHECK(IsPresent());
  BluetoothDeviceBlueZ* device_bluez = GetDeviceWithPath(object_path);
  if (!device_bluez) {
    LOG(WARNING) << "Pairing Agent request for unknown device: "
                 << object_path.value();
    return nullptr;
  }

  BluetoothPairingBlueZ* pairing = device_bluez->GetPairing();
  if (pairing)
    return pairing;

  // The device doesn't have its own pairing context, so this is an incoming
  // pairing request that should use our best default delegate (if we have one).
  BluetoothDevice::PairingDelegate* pairing_delegate = DefaultPairingDelegate();
  if (!pairing_delegate)
    return nullptr;

  return device_bluez->BeginPairing(pairing_delegate);
}

void BluetoothAdapterBlueZ::SetAdapter(const dbus::ObjectPath& object_path) {
  DCHECK(!IsPresent());
  DCHECK(!dbus_is_shutdown_);
  object_path_ = object_path;

  VLOG(1) << object_path_.value() << ": using adapter.";

  VLOG(1) << "Registering pairing agent";
  bluez::BluezDBusManager::Get()
      ->GetBluetoothAgentManagerClient()
      ->RegisterAgent(dbus::ObjectPath(kAgentPath),
                      bluetooth_agent_manager::kKeyboardDisplayCapability,
                      base::Bind(&BluetoothAdapterBlueZ::OnRegisterAgent,
                                 weak_ptr_factory_.GetWeakPtr()),
                      base::Bind(&BluetoothAdapterBlueZ::OnRegisterAgentError,
                                 weak_ptr_factory_.GetWeakPtr()));

#if defined(OS_CHROMEOS)
  SetStandardChromeOSAdapterName();
#endif

  bluez::BluetoothAdapterClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothAdapterClient()
          ->GetProperties(object_path_);

  PresentChanged(true);

  if (properties->powered.value())
    NotifyAdapterPoweredChanged(true);
  if (properties->discoverable.value())
    DiscoverableChanged(true);
  if (properties->discovering.value())
    DiscoveringChanged(true);

  std::vector<dbus::ObjectPath> device_paths =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothDeviceClient()
          ->GetDevicesForAdapter(object_path_);

  for (std::vector<dbus::ObjectPath>::iterator iter = device_paths.begin();
       iter != device_paths.end(); ++iter) {
    DeviceAdded(*iter);
  }
}

#if defined(OS_CHROMEOS)
void BluetoothAdapterBlueZ::SetStandardChromeOSAdapterName() {
  DCHECK(IsPresent());

  std::string alias;
  switch (chromeos::GetDeviceType()) {
    case chromeos::DeviceType::kChromebase:
      alias = "Chromebase";
      break;
    case chromeos::DeviceType::kChromebit:
      alias = "Chromebit";
      break;
    case chromeos::DeviceType::kChromebook:
      alias = "Chromebook";
      break;
    case chromeos::DeviceType::kChromebox:
      alias = "Chromebox";
      break;
    case chromeos::DeviceType::kUnknown:
      alias = "Chromebook";
      break;
  }
  // Take the lower 2 bytes of hashed Bluetooth address and combine it with the
  // device type to create a more identifiable device name.
  const std::string address = GetAddress();
  alias = base::StringPrintf(
      "%s_%04X", alias.c_str(),
      base::SuperFastHash(address.data(), address.size()) & 0xFFFF);
  SetName(alias, base::Bind(&base::DoNothing), base::Bind(&base::DoNothing));
}
#endif

void BluetoothAdapterBlueZ::RemoveAdapter() {
  DCHECK(IsPresent());
  VLOG(1) << object_path_.value() << ": adapter removed.";

  bluez::BluetoothAdapterClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothAdapterClient()
          ->GetProperties(object_path_);

  object_path_ = dbus::ObjectPath("");

  if (properties->powered.value())
    NotifyAdapterPoweredChanged(false);
  if (properties->discoverable.value())
    DiscoverableChanged(false);
  if (properties->discovering.value())
    DiscoveringChanged(false);

  // Move all elements of the original devices list to a new list here,
  // leaving the original list empty so that when we send DeviceRemoved(),
  // GetDevices() returns no devices.
  DevicesMap devices_swapped;
  devices_swapped.swap(devices_);

  for (auto& iter : devices_swapped) {
    FOR_EACH_OBSERVER(BluetoothAdapter::Observer, observers_,
                      DeviceRemoved(this, iter.second));
  }

  PresentChanged(false);
}

void BluetoothAdapterBlueZ::DiscoverableChanged(bool discoverable) {
  FOR_EACH_OBSERVER(BluetoothAdapter::Observer, observers_,
                    AdapterDiscoverableChanged(this, discoverable));
}

void BluetoothAdapterBlueZ::DiscoveringChanged(bool discovering) {
  // If the adapter stopped discovery due to a reason other than a request by
  // us, reset the count to 0.
  VLOG(1) << "Discovering changed: " << discovering;
  if (!discovering && !discovery_request_pending_ &&
      num_discovery_sessions_ > 0) {
    VLOG(1) << "Marking sessions as inactive.";
    num_discovery_sessions_ = 0;
    MarkDiscoverySessionsAsInactive();
  }
  FOR_EACH_OBSERVER(BluetoothAdapter::Observer, observers_,
                    AdapterDiscoveringChanged(this, discovering));
}

void BluetoothAdapterBlueZ::PresentChanged(bool present) {
  FOR_EACH_OBSERVER(BluetoothAdapter::Observer, observers_,
                    AdapterPresentChanged(this, present));
}

void BluetoothAdapterBlueZ::NotifyDeviceAddressChanged(
    BluetoothDeviceBlueZ* device,
    const std::string& old_address) {
  DCHECK(device->adapter_ == this);

  FOR_EACH_OBSERVER(BluetoothAdapter::Observer, observers_,
                    DeviceAddressChanged(this, device, old_address));
}

void BluetoothAdapterBlueZ::UseProfile(
    const BluetoothUUID& uuid,
    const dbus::ObjectPath& device_path,
    const bluez::BluetoothProfileManagerClient::Options& options,
    bluez::BluetoothProfileServiceProvider::Delegate* delegate,
    const ProfileRegisteredCallback& success_callback,
    const ErrorCompletionCallback& error_callback) {
  DCHECK(delegate);

  if (!IsPresent()) {
    VLOG(2) << "Adapter not present, erroring out";
    error_callback.Run("Adapter not present");
    return;
  }

  if (profiles_.find(uuid) != profiles_.end()) {
    // TODO(jamuraa) check that the options are the same and error when they are
    // not.
    SetProfileDelegate(uuid, device_path, delegate, success_callback,
                       error_callback);
    return;
  }

  if (profile_queues_.find(uuid) == profile_queues_.end()) {
    BluetoothAdapterProfileBlueZ::Register(
        uuid, options,
        base::Bind(&BluetoothAdapterBlueZ::OnRegisterProfile, this, uuid),
        base::Bind(&BluetoothAdapterBlueZ::OnRegisterProfileError, this, uuid));

    profile_queues_[uuid] = new std::vector<RegisterProfileCompletionPair>();
  }

  profile_queues_[uuid]->push_back(std::make_pair(
      base::Bind(&BluetoothAdapterBlueZ::SetProfileDelegate, this, uuid,
                 device_path, delegate, success_callback, error_callback),
      error_callback));
}

void BluetoothAdapterBlueZ::ReleaseProfile(
    const dbus::ObjectPath& device_path,
    BluetoothAdapterProfileBlueZ* profile) {
  VLOG(2) << "Releasing Profile: " << profile->uuid().canonical_value()
          << " from " << device_path.value();
  BluetoothUUID uuid = profile->uuid();
  auto iter = profiles_.find(uuid);
  if (iter == profiles_.end()) {
    LOG(ERROR) << "Profile not found for: " << uuid.canonical_value();
    return;
  }
  released_profiles_[uuid] = iter->second;
  profiles_.erase(iter);
  profile->RemoveDelegate(device_path,
                          base::Bind(&BluetoothAdapterBlueZ::RemoveProfile,
                                     weak_ptr_factory_.GetWeakPtr(), uuid));
}

void BluetoothAdapterBlueZ::RemoveProfile(const BluetoothUUID& uuid) {
  VLOG(2) << "Remove Profile: " << uuid.canonical_value();

  auto iter = released_profiles_.find(uuid);
  if (iter == released_profiles_.end()) {
    LOG(ERROR) << "Released Profile not found: " << uuid.canonical_value();
    return;
  }
  delete iter->second;
  released_profiles_.erase(iter);
}

void BluetoothAdapterBlueZ::AddLocalGattService(
    std::unique_ptr<BluetoothLocalGattServiceBlueZ> service) {
  owned_gatt_services_[service->object_path()] = std::move(service);
}

void BluetoothAdapterBlueZ::RemoveLocalGattService(
    BluetoothLocalGattServiceBlueZ* service) {
  auto service_iter = owned_gatt_services_.find(service->object_path());
  if (service_iter == owned_gatt_services_.end()) {
    LOG(WARNING) << "Trying to remove service: "
                 << service->object_path().value()
                 << " from adapter: " << object_path_.value()
                 << " that doesn't own it.";
    return;
  }

  if (registered_gatt_services_.count(service->object_path()) != 0) {
    registered_gatt_services_.erase(service->object_path());
    UpdateRegisteredApplication(true, base::Bind(&base::DoNothing),
                                base::Bind(&DoNothingOnError));
  }

  owned_gatt_services_.erase(service_iter);
}

void BluetoothAdapterBlueZ::RegisterGattService(
    BluetoothLocalGattServiceBlueZ* service,
    const base::Closure& callback,
    const device::BluetoothGattService::ErrorCallback& error_callback) {
  if (registered_gatt_services_.count(service->object_path()) > 0) {
    LOG(WARNING) << "Re-registering a service that is already registered!";
    error_callback.Run(device::BluetoothGattService::GATT_ERROR_FAILED);
    return;
  }

  registered_gatt_services_[service->object_path()] = service;

  // Always assume that we were already registered. If we weren't registered
  // we'll just get an error back which we can ignore. Any other approach will
  // introduce a race since we will always have a period when we may have been
  // registered with BlueZ, but not know that the registration succeeded
  // because the callback hasn't come back yet.
  UpdateRegisteredApplication(true, callback, error_callback);
}

void BluetoothAdapterBlueZ::UnregisterGattService(
    BluetoothLocalGattServiceBlueZ* service,
    const base::Closure& callback,
    const device::BluetoothGattService::ErrorCallback& error_callback) {
  DCHECK(bluez::BluezDBusManager::Get());

  if (registered_gatt_services_.count(service->object_path()) == 0) {
    LOG(WARNING) << "Unregistering a service that isn't registered! path: "
                 << service->object_path().value();
    error_callback.Run(device::BluetoothGattService::GATT_ERROR_FAILED);
    return;
  }

  registered_gatt_services_.erase(service->object_path());
  UpdateRegisteredApplication(false, callback, error_callback);
}

bool BluetoothAdapterBlueZ::IsGattServiceRegistered(
    BluetoothLocalGattServiceBlueZ* service) {
  return registered_gatt_services_.count(service->object_path()) != 0;
}

bool BluetoothAdapterBlueZ::SendValueChanged(
    BluetoothLocalGattCharacteristicBlueZ* characteristic,
    const std::vector<uint8_t>& value) {
  if (registered_gatt_services_.count(
          static_cast<BluetoothLocalGattServiceBlueZ*>(
              characteristic->GetService())
              ->object_path()) == 0)
    return false;
  gatt_application_provider_->SendValueChanged(characteristic->object_path(),
                                               value);
  return true;
}

dbus::ObjectPath BluetoothAdapterBlueZ::GetApplicationObjectPath() const {
  return dbus::ObjectPath(object_path_.value() + kGattApplicationObjectPath);
}

void BluetoothAdapterBlueZ::OnRegisterProfile(
    const BluetoothUUID& uuid,
    std::unique_ptr<BluetoothAdapterProfileBlueZ> profile) {
  profiles_[uuid] = profile.release();

  if (profile_queues_.find(uuid) == profile_queues_.end())
    return;

  for (auto& it : *profile_queues_[uuid])
    it.first.Run();
  delete profile_queues_[uuid];
  profile_queues_.erase(uuid);
}

void BluetoothAdapterBlueZ::SetProfileDelegate(
    const BluetoothUUID& uuid,
    const dbus::ObjectPath& device_path,
    bluez::BluetoothProfileServiceProvider::Delegate* delegate,
    const ProfileRegisteredCallback& success_callback,
    const ErrorCompletionCallback& error_callback) {
  if (profiles_.find(uuid) == profiles_.end()) {
    error_callback.Run("Cannot find profile!");
    return;
  }

  if (profiles_[uuid]->SetDelegate(device_path, delegate)) {
    success_callback.Run(profiles_[uuid]);
    return;
  }
  // Already set
  error_callback.Run(bluetooth_agent_manager::kErrorAlreadyExists);
}

void BluetoothAdapterBlueZ::OnRegisterProfileError(
    const BluetoothUUID& uuid,
    const std::string& error_name,
    const std::string& error_message) {
  VLOG(2) << object_path_.value()
          << ": Failed to register profile: " << error_name << ": "
          << error_message;
  if (profile_queues_.find(uuid) == profile_queues_.end())
    return;

  for (auto& it : *profile_queues_[uuid])
    it.second.Run(error_message);

  delete profile_queues_[uuid];
  profile_queues_.erase(uuid);
}

void BluetoothAdapterBlueZ::OnSetDiscoverable(
    const base::Closure& callback,
    const ErrorCallback& error_callback,
    bool success) {
  if (!IsPresent()) {
    error_callback.Run();
    return;
  }

  // Set the discoverable_timeout property to zero so the adapter remains
  // discoverable forever.
  bluez::BluezDBusManager::Get()
      ->GetBluetoothAdapterClient()
      ->GetProperties(object_path_)
      ->discoverable_timeout.Set(
          0,
          base::Bind(&BluetoothAdapterBlueZ::OnPropertyChangeCompleted,
                     weak_ptr_factory_.GetWeakPtr(), callback, error_callback));
}

void BluetoothAdapterBlueZ::OnPropertyChangeCompleted(
    const base::Closure& callback,
    const ErrorCallback& error_callback,
    bool success) {
  if (IsPresent() && success) {
    callback.Run();
  } else {
    error_callback.Run();
  }
}

void BluetoothAdapterBlueZ::AddDiscoverySession(
    BluetoothDiscoveryFilter* discovery_filter,
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback) {
  if (!IsPresent()) {
    error_callback.Run(
        UMABluetoothDiscoverySessionOutcome::ADAPTER_NOT_PRESENT);
    return;
  }
  VLOG(1) << __func__;
  if (discovery_request_pending_) {
    // The pending request is either to stop a previous session or to start a
    // new one. Either way, queue this one.
    DCHECK(num_discovery_sessions_ == 1 || num_discovery_sessions_ == 0);
    VLOG(1) << "Pending request to start/stop device discovery. Queueing "
            << "request to start a new discovery session.";
    discovery_request_queue_.push(
        std::make_tuple(discovery_filter, callback, error_callback));
    return;
  }

  // The adapter is already discovering.
  if (num_discovery_sessions_ > 0) {
    DCHECK(IsDiscovering());
    DCHECK(!discovery_request_pending_);
    num_discovery_sessions_++;
    SetDiscoveryFilter(BluetoothDiscoveryFilter::Merge(
                           GetMergedDiscoveryFilter().get(), discovery_filter),
                       callback, error_callback);
    return;
  }

  // There are no active discovery sessions.
  DCHECK_EQ(num_discovery_sessions_, 0);

  if (discovery_filter) {
    discovery_request_pending_ = true;

    std::unique_ptr<BluetoothDiscoveryFilter> df(
        new BluetoothDiscoveryFilter(device::BLUETOOTH_TRANSPORT_DUAL));
    df->CopyFrom(*discovery_filter);
    SetDiscoveryFilter(
        std::move(df),
        base::Bind(&BluetoothAdapterBlueZ::OnPreSetDiscoveryFilter,
                   weak_ptr_factory_.GetWeakPtr(), callback, error_callback),
        base::Bind(&BluetoothAdapterBlueZ::OnPreSetDiscoveryFilterError,
                   weak_ptr_factory_.GetWeakPtr(), callback, error_callback));
    return;
  } else {
    current_filter_.reset();
  }

  // This is the first request to start device discovery.
  discovery_request_pending_ = true;
  bluez::BluezDBusManager::Get()->GetBluetoothAdapterClient()->StartDiscovery(
      object_path_,
      base::Bind(&BluetoothAdapterBlueZ::OnStartDiscovery,
                 weak_ptr_factory_.GetWeakPtr(), callback, error_callback),
      base::Bind(&BluetoothAdapterBlueZ::OnStartDiscoveryError,
                 weak_ptr_factory_.GetWeakPtr(), callback, error_callback));
}

void BluetoothAdapterBlueZ::RemoveDiscoverySession(
    BluetoothDiscoveryFilter* discovery_filter,
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback) {
  if (!IsPresent()) {
    error_callback.Run(
        UMABluetoothDiscoverySessionOutcome::ADAPTER_NOT_PRESENT);
    return;
  }

  VLOG(1) << __func__;
  // There are active sessions other than the one currently being removed.
  if (num_discovery_sessions_ > 1) {
    DCHECK(IsDiscovering());
    DCHECK(!discovery_request_pending_);
    num_discovery_sessions_--;

    SetDiscoveryFilter(GetMergedDiscoveryFilterMasked(discovery_filter),
                       callback, error_callback);
    return;
  }

  // If there is a pending request to BlueZ, then queue this request.
  if (discovery_request_pending_) {
    VLOG(1) << "Pending request to start/stop device discovery. Queueing "
            << "request to stop discovery session.";
    error_callback.Run(
        UMABluetoothDiscoverySessionOutcome::REMOVE_WITH_PENDING_REQUEST);
    return;
  }

  // There are no active sessions. Return error.
  if (num_discovery_sessions_ == 0) {
    // TODO(armansito): This should never happen once we have the
    // DiscoverySession API. Replace this case with an assert once it's
    // the deprecated methods have been removed. (See crbug.com/3445008).
    VLOG(1) << "No active discovery sessions. Returning error.";
    error_callback.Run(
        UMABluetoothDiscoverySessionOutcome::ACTIVE_SESSION_NOT_IN_ADAPTER);
    return;
  }

  // There is exactly one active discovery session. Request BlueZ to stop
  // discovery.
  DCHECK_EQ(num_discovery_sessions_, 1);
  discovery_request_pending_ = true;
  bluez::BluezDBusManager::Get()->GetBluetoothAdapterClient()->StopDiscovery(
      object_path_, base::Bind(&BluetoothAdapterBlueZ::OnStopDiscovery,
                               weak_ptr_factory_.GetWeakPtr(), callback),
      base::Bind(&BluetoothAdapterBlueZ::OnStopDiscoveryError,
                 weak_ptr_factory_.GetWeakPtr(), error_callback));
}

void BluetoothAdapterBlueZ::SetDiscoveryFilter(
    std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter,
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback) {
  if (!IsPresent()) {
    error_callback.Run(UMABluetoothDiscoverySessionOutcome::ADAPTER_REMOVED);
    return;
  }

  // If old and new filter are equal (null) then don't make request, just call
  // succes callback
  if (!current_filter_ && !discovery_filter.get()) {
    callback.Run();
    return;
  }

  // If old and new filter are not null and equal then don't make request, just
  // call succes callback
  if (current_filter_ && discovery_filter &&
      current_filter_->Equals(*discovery_filter)) {
    callback.Run();
    return;
  }

  current_filter_.reset(discovery_filter.release());

  bluez::BluetoothAdapterClient::DiscoveryFilter dbus_discovery_filter;

  if (current_filter_.get()) {
    uint16_t pathloss;
    int16_t rssi;
    uint8_t transport;
    std::set<device::BluetoothUUID> uuids;

    if (current_filter_->GetPathloss(&pathloss))
      dbus_discovery_filter.pathloss.reset(new uint16_t(pathloss));

    if (current_filter_->GetRSSI(&rssi))
      dbus_discovery_filter.rssi.reset(new int16_t(rssi));

    transport = current_filter_->GetTransport();
    if (transport == device::BLUETOOTH_TRANSPORT_LE) {
      dbus_discovery_filter.transport.reset(new std::string("le"));
    } else if (transport == device::BLUETOOTH_TRANSPORT_CLASSIC) {
      dbus_discovery_filter.transport.reset(new std::string("bredr"));
    } else if (transport == device::BLUETOOTH_TRANSPORT_DUAL) {
      dbus_discovery_filter.transport.reset(new std::string("auto"));
    }

    current_filter_->GetUUIDs(uuids);
    if (uuids.size()) {
      dbus_discovery_filter.uuids = std::unique_ptr<std::vector<std::string>>(
          new std::vector<std::string>);

      for (const auto& it : uuids)
        dbus_discovery_filter.uuids.get()->push_back(it.value());
    }
  }

  bluez::BluezDBusManager::Get()
      ->GetBluetoothAdapterClient()
      ->SetDiscoveryFilter(
          object_path_, dbus_discovery_filter,
          base::Bind(&BluetoothAdapterBlueZ::OnSetDiscoveryFilter,
                     weak_ptr_factory_.GetWeakPtr(), callback, error_callback),
          base::Bind(&BluetoothAdapterBlueZ::OnSetDiscoveryFilterError,
                     weak_ptr_factory_.GetWeakPtr(), callback, error_callback));
}

void BluetoothAdapterBlueZ::OnStartDiscovery(
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback) {
  // Report success on the original request and increment the count.
  VLOG(1) << __func__;
  DCHECK(discovery_request_pending_);
  DCHECK_EQ(num_discovery_sessions_, 0);
  discovery_request_pending_ = false;
  num_discovery_sessions_++;
  if (IsPresent()) {
    callback.Run();
  } else {
    error_callback.Run(UMABluetoothDiscoverySessionOutcome::ADAPTER_REMOVED);
  }

  // Try to add a new discovery session for each queued request.
  ProcessQueuedDiscoveryRequests();
}

void BluetoothAdapterBlueZ::OnStartDiscoveryError(
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback,
    const std::string& error_name,
    const std::string& error_message) {
  LOG(WARNING) << object_path_.value()
               << ": Failed to start discovery: " << error_name << ": "
               << error_message;

  // Failed to start discovery. This can only happen if the count is at 0.
  DCHECK_EQ(num_discovery_sessions_, 0);
  DCHECK(discovery_request_pending_);
  discovery_request_pending_ = false;

  // Discovery request may fail if discovery was previously initiated by Chrome,
  // but the session were invalidated due to the discovery state unexpectedly
  // changing to false and then back to true. In this case, report success.
  if (IsPresent() && error_name == bluetooth_device::kErrorInProgress &&
      IsDiscovering()) {
    VLOG(1) << "Discovery previously initiated. Reporting success.";
    num_discovery_sessions_++;
    callback.Run();
  } else {
    error_callback.Run(TranslateDiscoveryErrorToUMA(error_name));
  }

  // Try to add a new discovery session for each queued request.
  ProcessQueuedDiscoveryRequests();
}

void BluetoothAdapterBlueZ::OnStopDiscovery(const base::Closure& callback) {
  // Report success on the original request and decrement the count.
  VLOG(1) << __func__;
  DCHECK(discovery_request_pending_);
  DCHECK_EQ(num_discovery_sessions_, 1);
  discovery_request_pending_ = false;
  num_discovery_sessions_--;
  callback.Run();

  current_filter_.reset();

  // Try to add a new discovery session for each queued request.
  ProcessQueuedDiscoveryRequests();
}

void BluetoothAdapterBlueZ::OnStopDiscoveryError(
    const DiscoverySessionErrorCallback& error_callback,
    const std::string& error_name,
    const std::string& error_message) {
  LOG(WARNING) << object_path_.value()
               << ": Failed to stop discovery: " << error_name << ": "
               << error_message;

  // Failed to stop discovery. This can only happen if the count is at 1.
  DCHECK(discovery_request_pending_);
  DCHECK_EQ(num_discovery_sessions_, 1);
  discovery_request_pending_ = false;
  error_callback.Run(TranslateDiscoveryErrorToUMA(error_name));

  // Try to add a new discovery session for each queued request.
  ProcessQueuedDiscoveryRequests();
}

void BluetoothAdapterBlueZ::OnPreSetDiscoveryFilter(
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback) {
  // This is the first request to start device discovery.
  DCHECK(discovery_request_pending_);
  DCHECK_EQ(num_discovery_sessions_, 0);

  bluez::BluezDBusManager::Get()->GetBluetoothAdapterClient()->StartDiscovery(
      object_path_,
      base::Bind(&BluetoothAdapterBlueZ::OnStartDiscovery,
                 weak_ptr_factory_.GetWeakPtr(), callback, error_callback),
      base::Bind(&BluetoothAdapterBlueZ::OnStartDiscoveryError,
                 weak_ptr_factory_.GetWeakPtr(), callback, error_callback));
}

void BluetoothAdapterBlueZ::OnPreSetDiscoveryFilterError(
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback,
    UMABluetoothDiscoverySessionOutcome outcome) {
  LOG(WARNING) << object_path_.value()
               << ": Failed to pre set discovery filter.";

  // Failed to start discovery. This can only happen if the count is at 0.
  DCHECK_EQ(num_discovery_sessions_, 0);
  DCHECK(discovery_request_pending_);
  discovery_request_pending_ = false;

  error_callback.Run(outcome);

  // Try to add a new discovery session for each queued request.
  ProcessQueuedDiscoveryRequests();
}

void BluetoothAdapterBlueZ::OnSetDiscoveryFilter(
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback) {
  // Report success on the original request and increment the count.
  VLOG(1) << __func__;
  if (IsPresent()) {
    callback.Run();
  } else {
    error_callback.Run(UMABluetoothDiscoverySessionOutcome::ADAPTER_REMOVED);
  }
}

void BluetoothAdapterBlueZ::OnSetDiscoveryFilterError(
    const base::Closure& callback,
    const DiscoverySessionErrorCallback& error_callback,
    const std::string& error_name,
    const std::string& error_message) {
  LOG(WARNING) << object_path_.value()
               << ": Failed to set discovery filter: " << error_name << ": "
               << error_message;

  UMABluetoothDiscoverySessionOutcome outcome =
      TranslateDiscoveryErrorToUMA(error_name);
  if (outcome == UMABluetoothDiscoverySessionOutcome::FAILED) {
    // bluez/doc/adapter-api.txt says "Failed" is returned from
    // SetDiscoveryFilter when the controller doesn't support the requested
    // transport.
    outcome = UMABluetoothDiscoverySessionOutcome::
        BLUEZ_DBUS_FAILED_MAYBE_UNSUPPORTED_TRANSPORT;
  }
  error_callback.Run(outcome);

  // Try to add a new discovery session for each queued request.
  ProcessQueuedDiscoveryRequests();
}

void BluetoothAdapterBlueZ::ProcessQueuedDiscoveryRequests() {
  while (!discovery_request_queue_.empty()) {
    VLOG(1) << "Process queued discovery request.";
    DiscoveryParamTuple params = discovery_request_queue_.front();
    discovery_request_queue_.pop();
    AddDiscoverySession(std::get<0>(params), std::get<1>(params),
                        std::get<2>(params));

    // If the queued request resulted in a pending call, then let it
    // asynchonously process the remaining queued requests once the pending
    // call returns.
    if (discovery_request_pending_)
      return;
  }
}

void BluetoothAdapterBlueZ::UpdateRegisteredApplication(
    bool ignore_unregister_failure,
    const base::Closure& callback,
    const device::BluetoothGattService::ErrorCallback& error_callback) {
  // If ignore_unregister_failure is set, we'll forward the error_callback to
  // the register call (to be called in case the register call fails). If not,
  // we'll call the error callback if this unregister itself fails.
  bluez::BluezDBusManager::Get()
      ->GetBluetoothGattManagerClient()
      ->UnregisterApplication(
          object_path_, GetApplicationObjectPath(),
          base::Bind(&BluetoothAdapterBlueZ::RegisterApplication,
                     weak_ptr_factory_.GetWeakPtr(), callback, error_callback),
          ignore_unregister_failure
              ? base::Bind(&BluetoothAdapterBlueZ::RegisterApplicationOnError,
                           weak_ptr_factory_.GetWeakPtr(), callback,
                           error_callback)
              : base::Bind(&OnRegisterationErrorCallback, error_callback,
                           false));
}

void BluetoothAdapterBlueZ::RegisterApplication(
    const base::Closure& callback,
    const device::BluetoothGattService::ErrorCallback& error_callback) {
  // Recreate our application service provider with the currently registered
  // GATT services before we register it.
  gatt_application_provider_.reset();
  // If we have no services registered, then leave the application unregistered
  // and no application provider.
  if (registered_gatt_services_.size() == 0) {
    callback.Run();
    return;
  }
  gatt_application_provider_ = BluetoothGattApplicationServiceProvider::Create(
      bluez::BluezDBusManager::Get()->GetSystemBus(),
      GetApplicationObjectPath(), registered_gatt_services_);

  DCHECK(bluez::BluezDBusManager::Get());
  bluez::BluezDBusManager::Get()
      ->GetBluetoothGattManagerClient()
      ->RegisterApplication(
          object_path_, GetApplicationObjectPath(),
          BluetoothGattManagerClient::Options(), callback,
          base::Bind(&OnRegisterationErrorCallback, error_callback, true));
}

void BluetoothAdapterBlueZ::RegisterApplicationOnError(
    const base::Closure& callback,
    const device::BluetoothGattService::ErrorCallback& error_callback,
    const std::string& /* error_name */,
    const std::string& /* error_message */) {
  RegisterApplication(callback, error_callback);
}

void BluetoothAdapterBlueZ::ServiceRecordErrorConnector(
    const ServiceRecordErrorCallback& error_callback,
    const std::string& error_name,
    const std::string& error_message) {
  VLOG(1) << "Creating service record failed: error: " << error_name << " - "
          << error_message;

  BluetoothServiceRecordBlueZ::ErrorCode code =
      BluetoothServiceRecordBlueZ::ErrorCode::UNKNOWN;
  if (error_name == bluetooth_adapter::kErrorInvalidArguments) {
    code = BluetoothServiceRecordBlueZ::ErrorCode::ERROR_INVALID_ARGUMENTS;
  } else if (error_name == bluetooth_adapter::kErrorDoesNotExist) {
    code = BluetoothServiceRecordBlueZ::ErrorCode::ERROR_RECORD_DOES_NOT_EXIST;
  } else if (error_name == bluetooth_adapter::kErrorAlreadyExists) {
    code = BluetoothServiceRecordBlueZ::ErrorCode::ERROR_RECORD_ALREADY_EXISTS;
  } else if (error_name == bluetooth_adapter::kErrorNotReady) {
    code = BluetoothServiceRecordBlueZ::ErrorCode::ERROR_ADAPTER_NOT_READY;
  }

  error_callback.Run(code);
}

}  // namespace bluez
