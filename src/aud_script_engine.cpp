// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include "aud_script_engine.hpp"

#include <cmath>

#include "aud_audio_script.h"

namespace aud_script {

std::unique_ptr<AudScriptContext> audScriptCreateContext(size_t memoryLimit,
                                                         AudScriptSink* sink,
                                                         std::string* error) {
  return audScriptCreateQuickJs(memoryLimit, sink, error);
}

const char* audScriptCheckSend(double output, double id, double value) {
  if (!(output >= 0 && output < AUD_SCRIPT_NUM_OUTPUTS) ||
      output != std::floor(output)) {
    return "aud.send: the output is an integer from 0 to 15";
  }
  if (!(id >= 0 && id < AUD_SCRIPT_NUM_IDS) || id != std::floor(id)) {
    return "aud.send: the id is an integer from 0 to 511";
  }
  if (!std::isfinite(value)) return "aud.send: the value is a finite number";
  return nullptr;
}

}  // namespace aud_script
