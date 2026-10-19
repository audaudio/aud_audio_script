// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include "aud_audio_script.h"

#include <cstring>
#include <string>

#include "aud_script_node.hpp"
#include "aud_script_stats.hpp"

namespace {

// Writes `text` with its zero into `buffer`.
int32_t copyText(const std::string& text, char* buffer, size_t capacity,
                 size_t* size) {
  if (size == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  *size = text.size();
  if (buffer == nullptr || capacity < text.size() + 1) {
    return AUD_ERROR_BUFFER_TOO_SMALL;
  }
  std::memcpy(buffer, text.c_str(), text.size() + 1);
  return AUD_OK;
}

}  // namespace

AUD_EXPORT int32_t aud_audio_script_register(const AudHostApi* host) {
  if (host == nullptr || host->register_node_type == nullptr) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  if (host->abi_major != AUD_ABI_VERSION_MAJOR) return AUD_ERROR_ABI_MAJOR;
  if (host->struct_size < sizeof(AudHostApi)) return AUD_ERROR_ABI_MINOR;
  return host->register_node_type(host->host,
                                  &aud_script::audScriptModulatorDescriptor());
}

AUD_EXPORT int32_t aud_audio_script_stats(AudScriptStats* stats) {
  if (stats == nullptr || stats->struct_size < sizeof(AudScriptStats)) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  aud_script::AudScriptGlobalStats::get().read(stats);
  return AUD_OK;
}

AUD_EXPORT int32_t aud_audio_script_last_error(char* buffer, size_t capacity,
                                               size_t* size) {
  return copyText(aud_script::AudScriptGlobalStats::get().error(), buffer,
                  capacity, size);
}

AUD_EXPORT int32_t aud_audio_script_console(char* buffer, size_t capacity,
                                            size_t* size) {
  return copyText(aud_script::AudScriptGlobalStats::get().console(), buffer,
                  capacity, size);
}

AUD_EXPORT void aud_audio_script_reset_stats(void) {
  aud_script::AudScriptGlobalStats::get().reset();
}
