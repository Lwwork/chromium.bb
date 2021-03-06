// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CHROME_INTERFACE_FACTORY_H_
#define CHROME_BROWSER_CHROMEOS_CHROME_INTERFACE_FACTORY_H_

#include "ash/sysui/public/interfaces/wallpaper.mojom.h"
#include "content/public/common/mojo_shell_connection.h"
#include "mash/public/interfaces/launchable.mojom.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/shell/public/cpp/interface_factory.h"
#include "services/shell/public/cpp/shell_client.h"
#include "ui/app_list/presenter/app_list_presenter.mojom.h"
#include "ui/keyboard/keyboard.mojom.h"

class AppListPresenterService;
class ChromeLaunchable;
class ChromeWallpaperManager;
class KeyboardUIService;

namespace chromeos {

// InterfaceFactory for creating all services provided by chrome.
class ChromeInterfaceFactory
    : public shell::ShellClient,
      public shell::InterfaceFactory<keyboard::mojom::Keyboard>,
      public shell::InterfaceFactory<mash::mojom::Launchable>,
      public shell::InterfaceFactory<ash::sysui::mojom::WallpaperManager>,
      public shell::InterfaceFactory<app_list::mojom::AppListPresenter> {
 public:
  ChromeInterfaceFactory();
  ~ChromeInterfaceFactory() override;

 private:
  // shell::ShellClient:
  bool AcceptConnection(shell::Connection* connection) override;

  // shell::InterfaceFactory<keyboard::Keyboard>:
  void Create(
      shell::Connection* connection,
      mojo::InterfaceRequest<keyboard::mojom::Keyboard> request) override;

  // mojo::InterfaceFactory<mash::mojom::Launchable>
  void Create(shell::Connection* connection,
              mash::mojom::LaunchableRequest request) override;

  // mojo::InterfaceFactory<ash::sysui::mojom::WallpaperManager>
  void Create(shell::Connection* connection,
              ash::sysui::mojom::WallpaperManagerRequest request) override;

  // mojo::InterfaceFactory<app_list::AppListPresenter>:
  void Create(shell::Connection* connection,
              mojo::InterfaceRequest<app_list::mojom::AppListPresenter> request)
      override;

  std::unique_ptr<KeyboardUIService> keyboard_ui_service_;
  mojo::BindingSet<keyboard::mojom::Keyboard> keyboard_bindings_;
  std::unique_ptr<ChromeLaunchable> launchable_;
  std::unique_ptr<ChromeWallpaperManager> wallpaper_manager_;
  std::unique_ptr<AppListPresenterService> app_list_presenter_service_;
  mojo::BindingSet<app_list::mojom::AppListPresenter>
      app_list_presenter_bindings_;

  DISALLOW_COPY_AND_ASSIGN(ChromeInterfaceFactory);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CHROME_INTERFACE_FACTORY_H_
