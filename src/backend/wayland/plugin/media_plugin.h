#pragma once

#include <string>
#include <vector>

#include "service/wayland/media_service.h"

extern "C" MediaDecodePlayback astralia_shell_media_plugin_stream(const std::string &path, const std::string &filter_desc, int fps, bool supports_row_length, MediaDecodeFrameCallback on_frame, MediaDecodeDrmFrameCallback on_drm_frame);

extern "C" std::vector<MediaFrame>
astralia_shell_media_plugin_frames(const std::string &path, const std::string &filter_desc, int max_frames);

extern "C" void astralia_shell_media_plugin_release_drm_frame(void *avframe_handle);
