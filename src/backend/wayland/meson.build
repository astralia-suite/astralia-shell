wayland_backend_source = files('backend.cpp')

wayland_sources = files(
  'app/config.cpp',
  'app/ipc.cpp',
  'app/key_dispatch.cpp',
  'app/module_registry.cpp',
  'app/monitor_output.cpp',
  'app/service_registry.cpp',
  'app/user_info.cpp',
  'app/wayland_registry.cpp',
  'core/log.cpp',
  'core/poll_source.cpp',
  '../../modules/bar/wayland/bar.cpp',
  '../../modules/bar/wayland/frame.cpp',
  '../../modules/bar/wayland/panel_surface.cpp',
  '../../modules/dashboard/wayland/dashboard.cpp',
  '../../modules/idle/wayland/idle.cpp',
  '../../modules/launcher/wayland/launcher.cpp',
  '../../modules/lock/wayland/lock.cpp',
  '../../modules/logout/wayland/logout.cpp',
  '../../modules/notification/wayland/notification.cpp',
  '../../modules/osd/wayland/osd.cpp',
  '../../modules/overview/wayland/overview.cpp',
  '../../modules/polkit/wayland/polkit.cpp',
  '../../modules/rain/wayland/rain.cpp',
  '../../modules/rain/wayland/matrix_rain.cpp',
  '../../modules/rain/wayland/stiletto_rain.cpp',
  '../../modules/settings/wayland/settings.cpp',
  '../../modules/visualizer/wayland/visualizer.cpp',
  '../../modules/visualizer/wayland/audio_capture.cpp',
  '../../modules/visualizer/wayland/audio_stages.cpp',
  '../../modules/visualizer/wayland/bar_visualizer.cpp',
  '../../modules/visualizer/wayland/fft.cpp',
  '../../modules/visualizer/wayland/sphere_visualizer.cpp',
  '../../modules/visualizer/wayland/visualizer_shaders.cpp',
  '../../modules/wallpaper/wayland/wallpaper.cpp',
  'render/animated_image.cpp',
  'render/arc_gauge.cpp',
  'render/gl.cpp',
  'render/gl_canvas.cpp',
  'render/icon.cpp',
  'render/image.cpp',
  'render/layer_surface.cpp',
  'render/overlay_panel.cpp',
  'render/popup_window.cpp',
  'render/renderer.cpp',
  'render/text.cpp',
  'render/texture.cpp',
  'render/texture_cache.cpp',
  'render/toplevel_window.cpp',
  'render/video_texture.cpp',
  '../../service/wayland/capture_service.cpp',
  '../../service/wayland/frame_service.cpp',
  '../../service/wayland/idle_service.cpp',
  '../../service/wayland/input_service.cpp',
  '../../service/wayland/media_service.cpp',
  '../../service/wayland/output_service.cpp',
  '../../service/wayland/settings_service.cpp',
  '../../service/wayland/text_input_service.cpp',
)

add_languages('c', native: false, required: true)

wayland_deps = [
  dependency('wayland-client'),
  dependency('wayland-egl'),
  dependency('egl'),
  dependency('glesv2'),
  dependency('cairo'),
  dependency('pangocairo'),
  dependency('xkbcommon'),
  dependency('libjpeg'),
  dependency('resvg', include_type: 'system'),
  dependency('libdrm'),
  dependency('glib-2.0'),
  dependency('gio-2.0'),
  dependency('gobject-2.0'),
  dependency('polkit-gobject-1'),
  meson.get_compiler('cpp').find_library('pam'),
]

wayland_data_dir = get_option('prefix') / get_option('datadir') / 'astralia-shell'

wayland_args = [
  '-DASTRALIA_SHELL_SHADER_DIR="@0@"'.format(wayland_data_dir / 'shaders'),
  '-DASTRALIA_SHELL_DEFAULT_WALLPAPER="@0@"'.format(wayland_data_dir / 'stellar-restoration.png'),
  '-DASTRALIA_SHELL_BULLET_DIR="@0@"'.format(wayland_data_dir / 'constellation'),
  '-DASTRALIA_SHELL_LOGOUT_LOGO="@0@"'.format(wayland_data_dir / 'logout' / 'logo.gif'),
  '-DASTRALIA_SHELL_LOGOUT_LOGO_STATIC="@0@"'.format(wayland_data_dir / 'logout' / 'logo.png'),
  '-DASTRALIA_SHELL_IDLE_LOGO="@0@"'.format(wayland_data_dir / 'stellar-restoration.svg'),
  '-DASTRALIA_SHELL_STILETTO_SPRITE="@0@"'.format(wayland_data_dir / 'stiletto.svg'),
  '-DASTRALIA_SHELL_INPUT_ECHO="@0@"'.format(wayland_data_dir / 'polkit' / 'electro.png'),
  '-DASTRALIA_SHELL_PAM_DIR="@0@"'.format(wayland_data_dir / 'pam'),
  '-DASTRALIA_SHELL_PROFILE_MEDIA="@0@"'.format(wayland_data_dir / 'gifs' / 'profile.gif'),
  '-DASTRALIA_SHELL_MEDIA_PLUGIN="@0@"'.format(wayland_data_dir / 'plugins' / 'libastralia-shell-media.so'),
]

wayland_scanner = find_program('wayland-scanner')
wl_proto_dir = dependency('wayland-protocols').get_variable(pkgconfig: 'pkgdatadir')

wayland_protocol_defs = [
  ['xdg-shell', wl_proto_dir / 'stable/xdg-shell/xdg-shell.xml', false],
  ['wlr-layer-shell-unstable-v1', files('protocols/wlr-layer-shell-unstable-v1.xml'), true],
  ['ext-idle-notify-v1', wl_proto_dir / 'staging/ext-idle-notify/ext-idle-notify-v1.xml', false],
  ['cursor-shape-v1', wl_proto_dir / 'staging/cursor-shape/cursor-shape-v1.xml', false],
  ['tablet-unstable-v2', wl_proto_dir / 'unstable/tablet/tablet-unstable-v2.xml', false],
  ['hyprland-toplevel-export-v1', files('protocols/hyprland-toplevel-export-v1.xml'), false],
  ['wlr-foreign-toplevel-management-unstable-v1', files('protocols/wlr-foreign-toplevel-management-unstable-v1.xml'), false],
  ['text-input-unstable-v3', files('protocols/text-input-unstable-v3.xml'), false],
  ['ext-session-lock-v1', wl_proto_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml', false],
]

wayland_protocol_sources = []
wayland_protocol_headers = []
foreach p : wayland_protocol_defs
  fixup = p[2] ? ' && sed -i s/namespace/name_space/g "$3"' : ''
  wayland_protocol_headers += custom_target(p[0] + '-client-header',
    input: p[1], output: p[0] + '-client-protocol.h',
    command: ['sh', '-c', '"$1" client-header "$2" "$3"' + fixup, '_', wayland_scanner.full_path(), '@INPUT@', '@OUTPUT@'])
  wayland_protocol_sources += custom_target(p[0] + '-client-source',
    input: p[1], output: p[0] + '-client-protocol.c',
    command: ['sh', '-c', '"$1" private-code "$2" "$3"' + fixup, '_', wayland_scanner.full_path(), '@INPUT@', '@OUTPUT@'])
endforeach

wayland_media_plugin_sources = files('plugin/media_plugin.cpp', 'core/log.cpp')
wayland_media_deps = [
  dependency('libavformat'),
  dependency('libavcodec'),
  dependency('libavfilter'),
  dependency('libavutil'),
  dependency('libdrm'),
]

install_data('../../../assets/logout/logo.gif', install_dir: wayland_data_dir / 'logout')
install_data(
  '../../../assets/stellar-restoration.png',
  '../../../assets/stellar-restoration.svg',
  '../../../assets/stiletto.svg',
  install_dir: wayland_data_dir,
)
install_subdir('../../../assets/shaders', install_dir: wayland_data_dir)
