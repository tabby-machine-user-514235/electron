// Copyright (c) 2026 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_ELECTRON_GEOLOCATION_CONTEXT_H_
#define ELECTRON_SHELL_BROWSER_ELECTRON_GEOLOCATION_CONTEXT_H_

#include <memory>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/device/public/mojom/geolocation_context.mojom.h"

namespace content {
class WebContents;
}

namespace electron {

class ElectronBrowserContext;
class ElectronGeolocationImpl;

// A geolocation context owned by an Electron WebContents. It deliberately
// implements the standard device Mojo interfaces so Blink continues to own
// timeout, maximumAge, watches and permission handling.
class ElectronGeolocationContext : public device::mojom::GeolocationContext {
 public:
  ElectronGeolocationContext(content::WebContents* web_contents,
                             ElectronBrowserContext* browser_context);
  ElectronGeolocationContext(const ElectronGeolocationContext&) = delete;
  ElectronGeolocationContext& operator=(const ElectronGeolocationContext&) =
      delete;
  ~ElectronGeolocationContext() override;

  void BindGeolocation(
      mojo::PendingReceiver<device::mojom::Geolocation> receiver,
      const url::Origin& requesting_origin,
      device::mojom::GeolocationClientId client_id,
      bool has_precise_permission) override;
  void OnPermissionUpdated(
      const url::Origin& origin,
      device::mojom::GeolocationPermissionLevel permission_level) override;
  void SetOverride(device::mojom::GeopositionResultPtr result) override;
  void ClearOverride() override;

  void OnConnectionError(ElectronGeolocationImpl* impl);
  void BindDefaultGeolocation(
      mojo::PendingReceiver<device::mojom::Geolocation> receiver,
      const url::Origin& requesting_origin,
      device::mojom::GeolocationClientId client_id,
      bool has_precise_permission);
  content::WebContents* web_contents() const { return web_contents_; }
  ElectronBrowserContext* browser_context() const { return browser_context_; }

 private:
  void OnProviderChanged();

  raw_ptr<content::WebContents> web_contents_;
  raw_ptr<ElectronBrowserContext> browser_context_;
  std::vector<std::unique_ptr<ElectronGeolocationImpl>> impls_;
  device::mojom::GeopositionResultPtr position_override_;
  mojo::Remote<device::mojom::GeolocationContext> default_context_;
  base::CallbackListSubscription provider_changed_subscription_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_ELECTRON_GEOLOCATION_CONTEXT_H_
