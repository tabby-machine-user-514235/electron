// Copyright (c) 2026 GitHub, Inc.
// Use of this source code is governed by the LICENSE file.

#include "shell/browser/electron_geolocation_context.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/device_service.h"
#include "content/public/browser/web_contents.h"
#include "gin/arguments.h"
#include "gin/converter.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/device/public/cpp/geolocation/geoposition.h"
#include "services/device/public/mojom/geolocation.mojom.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/common/gin_converters/value_converter.h"

namespace electron {

namespace {

device::mojom::GeopositionResultPtr PositionUnavailable(std::string message) {
  return device::mojom::GeopositionResult::NewError(
      device::mojom::GeopositionError::New(
          device::mojom::GeopositionErrorCode::kPositionUnavailable,
          std::move(message), ""));
}

bool ReadNumber(const base::DictValue& value,
                std::string_view name,
                double* output,
                bool required) {
  const std::optional<double> number = value.FindDouble(name);
  if (!number)
    return !required;
  *output = *number;
  return std::isfinite(*output);
}

device::mojom::GeopositionResultPtr ToResult(base::DictValue value) {
  auto position = device::mojom::Geoposition::New();
  if (!ReadNumber(value, "latitude", &position->latitude, true) ||
      !ReadNumber(value, "longitude", &position->longitude, true) ||
      !ReadNumber(value, "accuracy", &position->accuracy, true) ||
      position->accuracy < 0) {
    return PositionUnavailable("Invalid geolocation provider position");
  }
  ReadNumber(value, "altitude", &position->altitude, false);
  ReadNumber(value, "altitudeAccuracy", &position->altitude_accuracy, false);
  ReadNumber(value, "heading", &position->heading, false);
  ReadNumber(value, "speed", &position->speed, false);
  const std::optional<double> timestamp = value.FindDouble("timestamp");
  position->timestamp = base::Time::FromMillisecondsSinceUnixEpoch(
      timestamp.value_or(base::Time::Now().InMillisecondsFSinceUnixEpoch()));
  position->is_precise = true;
  if (!device::ValidateGeoposition(*position))
    return PositionUnavailable("Invalid geolocation provider position");
  return device::mojom::GeopositionResult::NewPosition(std::move(position));
}

}  // namespace

class ElectronGeolocationImpl final : public device::mojom::Geolocation {
 public:
  ElectronGeolocationImpl(
      mojo::PendingReceiver<device::mojom::Geolocation> receiver,
      const url::Origin& origin,
      ElectronGeolocationContext* context,
      device::mojom::GeolocationClientId client_id,
      bool has_precise_permission)
      : receiver_(this, std::move(receiver)),
        origin_(origin),
        context_(context),
        client_id_(client_id),
        has_precise_permission_(has_precise_permission) {
    receiver_.set_disconnect_handler(base::BindOnce(
        &ElectronGeolocationImpl::OnConnectionError, base::Unretained(this)));
  }

  ~ElectronGeolocationImpl() override {
    if (callback_)
      std::move(callback_).Run(PositionUnavailable("Geolocation disconnected"));
  }

  const url::Origin& origin() const { return origin_; }
  void SetOverride(const device::mojom::GeopositionResult& result) {
    override_ = result.Clone();
    current_ = result.Clone();
    Report();
  }
  void ClearOverride() { override_.reset(); }
  void OnPermissionUpdated(device::mojom::GeolocationPermissionLevel level) {
    if (level == device::mojom::GeolocationPermissionLevel::kDenied) {
      if (callback_) {
        std::move(callback_).Run(device::mojom::GeopositionResult::NewError(
            device::mojom::GeopositionError::New(
                device::mojom::GeopositionErrorCode::kPermissionDenied,
                "User denied geolocation permission", "")));
      }
      return;
    }
    has_precise_permission_ =
        level == device::mojom::GeolocationPermissionLevel::kPrecise;
  }
  void OnProviderChanged() {
    ++request_generation_;
    default_geolocation_.reset();
    if (callback_)
      RequestPosition();
  }

 private:
  void SetHighAccuracyHint(bool high_accuracy) override {
    high_accuracy_ = high_accuracy;
    if (default_geolocation_)
      default_geolocation_->SetHighAccuracyHint(high_accuracy_);
  }
  void QueryNextPosition(QueryNextPositionCallback callback) override {
    if (callback_) {
      OnConnectionError();
      return;
    }
    callback_ = std::move(callback);
    if (override_) {
      current_ = override_.Clone();
      Report();
      return;
    }
    RequestPosition();
  }
  void QueryCachedPosition(QueryCachedPositionCallback callback) override {
    if (override_) {
      std::move(callback).Run(override_.Clone());
    } else if (default_geolocation_) {
      default_geolocation_->QueryCachedPosition(std::move(callback));
    } else if (current_) {
      std::move(callback).Run(current_.Clone());
    } else {
      std::move(callback).Run(PositionUnavailable("No cached position"));
    }
  }
  void RequestPosition() {
    const uint64_t generation = ++request_generation_;
    const auto& provider = context_->browser_context()->geolocation_provider();
    if (!provider) {
      if (!default_geolocation_) {
        context_->BindDefaultGeolocation(
            default_geolocation_.BindNewPipeAndPassReceiver(), origin_,
            client_id_, has_precise_permission_);
        default_geolocation_->SetHighAccuracyHint(high_accuracy_);
      }
      default_geolocation_->QueryNextPosition(
          base::BindOnce(&ElectronGeolocationImpl::OnDefaultResult,
                         weak_factory_.GetWeakPtr(), generation));
      return;
    }
    provider.Run(context_->web_contents(),
                 context_->web_contents()
                     ->GetPrimaryMainFrame()
                     ->GetLastCommittedOrigin()
                     .Serialize(),
                 high_accuracy_ && has_precise_permission_,
                 base::BindOnce(&ElectronGeolocationImpl::OnProviderResult,
                                weak_factory_.GetWeakPtr(), generation));
  }
  void OnProviderResult(uint64_t generation, gin::Arguments* args) {
    if (generation != request_generation_ || !callback_)
      return;
    v8::Local<v8::Value> error;
    if (!args->GetNext(&error)) {
      current_ = PositionUnavailable("Geolocation provider did not respond");
      Report();
      return;
    }
    if (!error->IsNullOrUndefined()) {
      v8::Local<v8::String> message;
      if (error->ToString(args->isolate()->GetCurrentContext())
              .ToLocal(&message)) {
        current_ = PositionUnavailable(
            *v8::String::Utf8Value(args->isolate(), message));
      } else {
        current_ = PositionUnavailable("Geolocation provider failed");
      }
      Report();
      return;
    }
    v8::Local<v8::Value> raw_position;
    base::Value position_value;
    if (!args->GetNext(&raw_position) ||
        !gin::ConvertFromV8(args->isolate(), raw_position, &position_value) ||
        !position_value.is_dict()) {
      current_ = PositionUnavailable("Invalid geolocation provider position");
    } else {
      current_ = ToResult(std::move(position_value).TakeDict());
    }
    Report();
  }
  void OnDefaultResult(uint64_t generation,
                       device::mojom::GeopositionResultPtr result) {
    if (generation != request_generation_ || !callback_)
      return;
    current_ = std::move(result);
    Report();
  }
  void Report() {
    if (callback_ && current_)
      std::move(callback_).Run(std::move(current_));
  }
  void OnConnectionError() { context_->OnConnectionError(this); }

  mojo::Receiver<device::mojom::Geolocation> receiver_;
  const url::Origin origin_;
  raw_ptr<ElectronGeolocationContext> context_;
  const device::mojom::GeolocationClientId client_id_;
  QueryNextPositionCallback callback_;
  device::mojom::GeopositionResultPtr current_;
  device::mojom::GeopositionResultPtr override_;
  mojo::Remote<device::mojom::Geolocation> default_geolocation_;
  bool high_accuracy_ = false;
  bool has_precise_permission_ = false;
  uint64_t request_generation_ = 0;
  base::WeakPtrFactory<ElectronGeolocationImpl> weak_factory_{this};
};

ElectronGeolocationContext::ElectronGeolocationContext(
    content::WebContents* web_contents,
    ElectronBrowserContext* browser_context)
    : web_contents_(web_contents), browser_context_(browser_context) {
  provider_changed_subscription_ =
      browser_context_->AddGeolocationProviderChangedCallback(
          base::BindRepeating(&ElectronGeolocationContext::OnProviderChanged,
                              base::Unretained(this)));
}

ElectronGeolocationContext::~ElectronGeolocationContext() = default;

void ElectronGeolocationContext::BindGeolocation(
    mojo::PendingReceiver<device::mojom::Geolocation> receiver,
    const url::Origin& requesting_origin,
    device::mojom::GeolocationClientId client_id,
    bool has_precise_permission) {
  auto impl = std::make_unique<ElectronGeolocationImpl>(
      std::move(receiver), requesting_origin, this, client_id,
      has_precise_permission);
  if (position_override_)
    impl->SetOverride(*position_override_);
  impls_.push_back(std::move(impl));
}

void ElectronGeolocationContext::OnPermissionUpdated(
    const url::Origin& origin,
    device::mojom::GeolocationPermissionLevel level) {
  if (default_context_)
    default_context_->OnPermissionUpdated(origin, level);
  for (auto& impl : impls_) {
    if (impl->origin() == origin)
      impl->OnPermissionUpdated(level);
  }
}

void ElectronGeolocationContext::SetOverride(
    device::mojom::GeopositionResultPtr result) {
  position_override_ = std::move(result);
  for (auto& impl : impls_)
    impl->SetOverride(*position_override_);
}

void ElectronGeolocationContext::ClearOverride() {
  position_override_.reset();
  for (auto& impl : impls_)
    impl->ClearOverride();
}

void ElectronGeolocationContext::OnConnectionError(
    ElectronGeolocationImpl* impl) {
  std::erase_if(impls_,
                [impl](const auto& entry) { return entry.get() == impl; });
}

void ElectronGeolocationContext::BindDefaultGeolocation(
    mojo::PendingReceiver<device::mojom::Geolocation> receiver,
    const url::Origin& requesting_origin,
    device::mojom::GeolocationClientId client_id,
    bool has_precise_permission) {
  if (!default_context_) {
    content::GetDeviceService().BindGeolocationContext(
        default_context_.BindNewPipeAndPassReceiver());
  }
  default_context_->BindGeolocation(std::move(receiver), requesting_origin,
                                    client_id, has_precise_permission);
}

void ElectronGeolocationContext::OnProviderChanged() {
  for (auto& impl : impls_)
    impl->OnProviderChanged();
}

}  // namespace electron
