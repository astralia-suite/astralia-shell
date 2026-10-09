project(
  'astralia-shell',
  'cpp',
  version: '0.1.0',
  meson_version: '>=1.1',
  default_options: ['cpp_std=c++23', 'warning_level=3', 'optimization=2', 'debug=false'],
)

sdbus_cpp_dep = dependency('sdbus-c++', include_type: 'system')
threads_dep = dependency('threads')
libpipewire_dep = dependency('libpipewire-0.3', include_type: 'system')
polkit_agent_dep = dependency('polkit-agent-1', include_type: 'system')
pam_dep = meson.get_compiler('cpp').find_library('pam')

deps = [sdbus_cpp_dep, threads_dep, libpipewire_dep, polkit_agent_dep, pam_dep]

inc = include_directories('src')

add_project_arguments('-ffunction-sections', '-fdata-sections', language: 'cpp')
add_project_link_arguments('-Wl,--gc-sections', language: 'cpp')
hidden = ['-fvisibility=hidden', '-fvisibility-inlines-hidden']

data_dir = get_option('prefix') / get_option('datadir') / 'astralia-shell'
add_project_arguments(
  '-DASTRALIA_DEFAULT_WALLPAPER="@0@"'.format(data_dir / 'stellar-restoration.png'),
  '-DASTRALIA_ASSET_DIR="@0@"'.format(data_dir),
  '-DASTRALIA_SOURCE_ASSET_DIR="@0@"'.format(meson.project_source_root() / 'assets'),
  language: 'cpp',
)

core_base = static_library(
  'astralia-core-base',
  files(
    'src/app/session.cpp',
    'src/app/shell.cpp',
    'src/core/allocator.cpp',
    'src/core/animation.cpp',
    'src/core/async_process.cpp',
    'src/core/cli.cpp',
    'src/core/config_file.cpp',
    'src/core/daemon.cpp',
    'src/core/dbus.cpp',
    'src/core/deferred_call.cpp',
    'src/core/ipc.cpp',
    'src/core/json.cpp',
    'src/core/log.cpp',
    'src/core/path_home.cpp',
    'src/core/poll_reactor.cpp',
    'src/core/runtime_paths.cpp',
    'src/core/single_instance.cpp',
    'src/core/spawn.cpp',
  ),
  include_directories: inc,
  dependencies: deps,
  pic: true,
)

core_shared = static_library(
  'astralia-core-shared',
  files(
    'src/modules/bar/model.cpp',
    'src/modules/bar/panel/battery_panel.cpp',
    'src/modules/bar/panel/bluetooth_panel.cpp',
    'src/modules/bar/panel/brightness_panel.cpp',
    'src/modules/bar/panel/clock_panel.cpp',
    'src/modules/bar/panel/media_panel.cpp',
    'src/modules/bar/panel/network_panel.cpp',
    'src/modules/bar/panel/panel.cpp',
    'src/modules/bar/panel/panel_set.cpp',
    'src/modules/bar/panel/resource_panel.cpp',
    'src/modules/bar/panel/tray_panel.cpp',
    'src/modules/bar/panel/volume_panel.cpp',
    'src/modules/bar/panel/widgets.cpp',
    'src/modules/bar/style.cpp',
    'src/modules/bar/view.cpp',
    'src/modules/launcher/apps_provider.cpp',
    'src/modules/launcher/desktop_entry.cpp',
    'src/modules/launcher/files_provider.cpp',
    'src/modules/launcher/launch_action.cpp',
    'src/modules/launcher/model.cpp',
    'src/modules/launcher/search.cpp',
    'src/modules/launcher/submenu.cpp',
    'src/modules/launcher/view.cpp',
    'src/modules/launcher/visit_store.cpp',
    'src/modules/lock/layout.cpp',
    'src/modules/lock/model.cpp',
    'src/modules/lock/pam_authenticator.cpp',
    'src/modules/lock/view.cpp',
    'src/modules/logout/layout.cpp',
    'src/modules/logout/model.cpp',
    'src/modules/logout/view.cpp',
    'src/modules/notification/model.cpp',
    'src/modules/notification/view.cpp',
    'src/modules/osd/model.cpp',
    'src/modules/osd/view.cpp',
    'src/modules/overview/model.cpp',
    'src/modules/overview/paging.cpp',
    'src/modules/overview/view.cpp',
    'src/modules/settings/model.cpp',
    'src/modules/settings/view.cpp',
    'src/modules/polkit/layout.cpp',
    'src/modules/polkit/model.cpp',
    'src/modules/polkit/view.cpp',
    'src/service/audio_service.cpp',
    'src/service/battery_service.cpp',
    'src/service/bluetooth_service.cpp',
    'src/service/brightness_service.cpp',
    'src/service/compositor_service.cpp',
    'src/service/dock_service.cpp',
    'src/service/hyprland_service.cpp',
    'src/service/i3_service.cpp',
    'src/service/icon_service.cpp',
    'src/service/media_service.cpp',
    'src/service/network_service.cpp',
    'src/service/notification_service.cpp',
    'src/service/polkit_service.cpp',
    'src/service/telemetry_service.cpp',
    'src/service/tray_service.cpp',
    'src/service/settings_service.cpp',
    'src/service/user_service.cpp',
    'src/service/wallpaper_service.cpp',
    'src/render/field_view.cpp',
    'src/render/marquee.cpp',
    'src/render/module_base.cpp',
    'src/render/text_field.cpp',
  ),
  include_directories: inc,
  dependencies: deps,
  cpp_args: hidden,
  pic: true,
)

dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)

backend_dir = get_option('prefix') / get_option('libdir') / 'astralia'

astralia_exe = executable(
  'astralia',
  'src/main.cpp',
  include_directories: inc,
  link_whole: core_base,
  dependencies: deps + [dl_dep],
  export_dynamic: true,
  cpp_args: ['-DASTRALIA_BACKEND_DIR="@0@"'.format(backend_dir)],
  install: true,
)

backend_targets = []

install_data(
  'assets/fonts/tabler-icons.ttf',
  'assets/fonts/ComicShannsMono-Regular.otf',
  'assets/fonts/YujiMai.ttf',
  install_dir: data_dir / 'fonts',
)
install_data('assets/logout/logo.png', install_dir: data_dir / 'logout')
install_data('assets/polkit/electro.png', install_dir: data_dir / 'polkit')
install_data('assets/gifs/profile.gif', install_dir: data_dir / 'gifs')
install_data('assets/pam/astralia-shell', install_dir: data_dir / 'pam')
install_data(
  'assets/constellation/C1.png',
  'assets/constellation/C2.png',
  'assets/constellation/C3.png',
  'assets/constellation/C4.png',
  'assets/constellation/C5.png',
  'assets/constellation/C6.png',
  install_dir: data_dir / 'constellation',
)

wayland_enabled = 'wayland' in get_option('backends')

if wayland_enabled
  subdir('src/backend/wayland')
  wayland_include = [inc, include_directories('src/backend')]
  wayland_lib = static_library(
    'astralia-wayland-lib',
    wayland_sources,
    wayland_protocol_sources,
    wayland_protocol_headers,
    include_directories: wayland_include,
    dependencies: deps + wayland_deps,
    cpp_args: wayland_args + hidden,
    pic: true,
  )
  backend_targets += shared_module(
    'astralia-wayland',
    wayland_backend_source,
    wayland_protocol_headers,
    name_prefix: 'lib',
    include_directories: wayland_include,
    dependencies: deps + wayland_deps,
    cpp_args: wayland_args + hidden,
    link_whole: wayland_lib,
    link_with: core_shared,
    install: true,
    install_dir: backend_dir,
  )
  backend_targets += shared_module(
    'astralia-shell-media',
    wayland_media_plugin_sources,
    include_directories: wayland_include,
    dependencies: wayland_media_deps,
    install: true,
    install_dir: data_dir / 'plugins',
  )
endif

x11_enabled = 'x11' in get_option('backends')

if x11_enabled
  subdir('src/backend/x11')
  x11_include = [inc, include_directories('src/backend/x11')]
  x11_lib = static_library(
    'astralia-x11-lib',
    x11_sources,
    include_directories: x11_include,
    dependencies: deps + x11_deps,
    cpp_args: x11_args + hidden,
    pic: true,
  )
  backend_targets += shared_module(
    'astralia-x11',
    x11_backend_source,
    name_prefix: 'lib',
    include_directories: x11_include,
    dependencies: deps + x11_deps,
    cpp_args: x11_args + hidden,
    link_whole: x11_lib,
    link_with: core_shared,
    install: true,
    install_dir: backend_dir,
  )
endif

unit = executable(
  'astralia-test',
  files(
    'test/main.cpp',
    'test/core/test_animation.cpp',
    'test/core/test_async_process.cpp',
    'test/core/test_config_file.cpp',
    'test/core/test_deferred_call.cpp',
    'test/core/test_input.cpp',
    'test/core/test_invocation.cpp',
    'test/core/test_json.cpp',
    'test/core/test_path_home.cpp',
    'test/core/test_reactor.cpp',
    'test/core/test_spawn.cpp',
    'test/render/test_module_base.cpp',
    'test/render/test_tokens.cpp',
    'test/modules/bar/test_bar.cpp',
    'test/modules/bar/test_panel.cpp',
    'test/modules/launcher/test_launcher.cpp',
    'test/modules/launcher/test_model.cpp',
    'test/modules/lock/test_lock.cpp',
    'test/modules/lock/test_layout.cpp',
    'test/modules/logout/test_layout.cpp',
    'test/modules/logout/test_logout.cpp',
    'test/modules/notification/test_notification.cpp',
    'test/modules/osd/test_osd.cpp',
    'test/modules/settings/test_settings.cpp',
    'test/modules/overview/test_overview.cpp',
    'test/modules/overview/test_paging.cpp',
    'test/modules/polkit/test_layout.cpp',
    'test/modules/polkit/test_polkit.cpp',
    'test/app/test_session.cpp',
    'test/app/test_shell.cpp',
    'test/service/test_compositor.cpp',
    'test/service/test_services.cpp',
    'test/service/test_settings.cpp',
    'test/service/test_wallpaper.cpp',
    'test/service/test_telemetry.cpp',
  ),
  include_directories: [inc, include_directories('test')],
  link_with: [core_base, core_shared],
  dependencies: deps + [dl_dep],
)

test('unit', unit)

if wayland_enabled
  wayland_unit = executable(
    'astralia-wayland-test',
    files(
      'test/backend/wayland/test_wayland.cpp',
      'test/backend/wayland/app/test_config.cpp',
      'test/backend/wayland/core/test_poll_source.cpp',
      'test/modules/bar/wayland/test_fillet.cpp',
      'test/modules/visualizer/wayland/test_fft.cpp',
      'test/backend/wayland/render/test_animated_image.cpp',
      'test/backend/wayland/render/test_image_decode.cpp',
      'test/backend/wayland/render/test_marquee_scroll.cpp',
      'test/backend/wayland/render/test_palette.cpp',
      'test/backend/wayland/render/test_text_elide.cpp',
      'test/backend/wayland/service/test_active_output.cpp',
      'test/backend/wayland/service/test_dock.cpp',
      'test/backend/wayland/service/test_input_convert.cpp',
      'test/backend/wayland/service/test_keyboard.cpp',
    ),
    wayland_protocol_headers,
    include_directories: [wayland_include, include_directories('test')],
    link_with: [wayland_lib, core_shared, core_base],
    dependencies: deps + wayland_deps + [dl_dep],
    cpp_args: wayland_args,
  )
  test('wayland-unit', wayland_unit)
endif

if x11_enabled
  x11_unit = executable(
    'astralia-x11-test',
    files('test/backend/x11/test_x11.cpp'),
    include_directories: [x11_include, include_directories('test')],
    link_with: [x11_lib, core_shared, core_base],
    dependencies: deps + x11_deps + [dl_dep],
    cpp_args: x11_args,
  )
  test('x11-unit', x11_unit)
endif
test('backend-smoke', find_program('bash'), args: [files('test/backend_smoke.sh'), astralia_exe], depends: backend_targets)
