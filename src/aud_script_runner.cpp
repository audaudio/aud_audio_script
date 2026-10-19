// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include "aud_script_runner.hpp"

#include <cmath>
#include <new>

#include "aud_clock.h"

namespace aud_script {

// ############################################################################
// The defaults and limits

bool audScriptValidTiming(double sampleRate, double tempo) {
  return sampleRate >= 8000 && sampleRate <= 384000 && tempo >= 1 &&
         tempo <= 999;
}

int64_t audScriptBudgetNs(uint32_t tickBudgetMs) {
  const uint32_t ms = tickBudgetMs > 0 ? tickBudgetMs : kDefaultTickBudgetMs;
  return static_cast<int64_t>(ms) * 1000000LL;
}

AudScriptLoad audScriptLoadOf(const char* source, const char* name,
                              uint32_t memoryLimitMb) {
  AudScriptLoad load;
  load.source = source;
  if (name != nullptr && name[0] != 0) load.name = name;
  load.memoryLimit =
      (memoryLimitMb > 0 ? memoryLimitMb : kDefaultMemoryLimitMb) * kMegabyte;
  return load;
}

// ############################################################################
// The tick grid

AudScriptGrid audScriptGrid(const AudScriptBlock& block) {
  AudScriptGrid grid;
  const double n = block.frames;
  const double t = block.tempo;
  const double sr = block.sampleRate;
  if (!block.playing || !(t > 0) || !(sr > 0) || block.frames == 0) {
    grid.beat = block.b0;
    return grid;
  }
  // The expressions and their order as the header writes them, so that
  // every implementation of the grid rounds alike.
  const double beatsPerBlock = n * t / 60 / sr;
  const double fractions = std::ceil(1 / beatsPerBlock);
  const double d = 1 / fractions;
  const double g = std::floor((block.b0 + beatsPerBlock) / d) * d;
  const double k = std::floor((g - block.b0) * 60 / t * sr);
  grid.beat = g;
  grid.offset = k >= n || !(k >= 0) ? 0 : static_cast<uint32_t>(k);
  return grid;
}

// ############################################################################
// The values

AudScriptValues::AudScriptValues()
    : index_(AUD_SCRIPT_NUM_OUTPUTS * AUD_SCRIPT_NUM_IDS, -1),
      logged_(AUD_SCRIPT_MAX_VALUES, 0) {
  pairs_.reserve(64);
  values_.reserve(64);
  undo_.reserve(AUD_SCRIPT_MAX_VALUES);
}

const char* AudScriptValues::set(uint32_t output, uint32_t id, float value) {
  const uint32_t pair = output * AUD_SCRIPT_NUM_IDS + id;
  const int16_t slot = index_[pair];
  if (slot >= 0) {
    // The value before the tick; later sends of the tick change nothing
    // that an undo needs.
    if (logged_[slot] != generation_) {
      logged_[slot] = generation_;
      undo_.push_back(Undo{static_cast<uint32_t>(slot), values_[slot], false});
    }
    values_[slot] = value;
    return nullptr;
  }
  if (pairs_.size() >= AUD_SCRIPT_MAX_VALUES) {
    return "aud.send: a script sends to at most 1024 (output, id) pairs";
  }
  const uint32_t added = static_cast<uint32_t>(pairs_.size());
  index_[pair] = static_cast<int16_t>(added);
  pairs_.push_back(static_cast<uint16_t>(pair));
  values_.push_back(value);
  logged_[added] = generation_;
  undo_.push_back(Undo{added, 0, true});
  return nullptr;
}

void AudScriptValues::rollback() {
  for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) {
    if (it->added) {
      index_[pairs_[it->slot]] = -1;
      pairs_.pop_back();
      values_.pop_back();
    } else {
      values_[it->slot] = it->old;
    }
  }
  restart();
}

// ############################################################################
// A loaded script

const char* AudScriptState::send(double output, double id, double value) {
  const char* message = audScriptCheckSend(output, id, value);
  if (message != nullptr) return message;
  const uint32_t o = static_cast<uint32_t>(output);
  const uint32_t i = static_cast<uint32_t>(id);
  message = values.set(o, i, static_cast<float>(value));
  if (message != nullptr) return message;
  const AudScriptSend record{o, i, value};
  if (!loading) {
    runner_->recordSend(record);
  } else if (runner_->sendLog != nullptr) {
    loadSends.push_back(record);
  }
  return nullptr;
}

void AudScriptState::log(const std::string& line) {
  runner_->console(name, line);
}

// ############################################################################
// The runner

void AudScriptRunner::console(const std::string& name,
                              const std::string& line) {
  if (onConsole) onConsole(name + ": " + line);
}

void AudScriptRunner::error(const std::string& name,
                            const std::string& message) {
  if (onError) onError(name + ": " + message);
}

std::unique_ptr<AudScriptState> AudScriptRunner::load(const AudScriptLoad& load,
                                                      AudScriptLoadTimes* times,
                                                      std::string* error) {
  std::unique_ptr<AudScriptState> state(new (std::nothrow)
                                            AudScriptState(this));
  std::string message;
  if (state == nullptr) {
    *error = load.name + ": out of memory";
    return nullptr;
  }
  state->name = load.name;
  state->memoryLimit = load.memoryLimit;
  const int64_t start = aud_clock_now_ns();
  state->context =
      audScriptCreateContext(load.memoryLimit, state.get(), &message);
  const int64_t created = aud_clock_now_ns();
  if (state->context == nullptr) {
    *error = load.name + ": " + message;
    return nullptr;
  }
  state->loading = true;
  state->values.begin();
  const int32_t status = state->context->evaluate(
      load.source, load.name, created + kLoadBudgetNs, &message);
  state->loading = false;
  const int64_t loaded = aud_clock_now_ns();
  if (status != AUD_SCRIPT_TICK_OK) {
    *error = load.name + ": " + message;
    return nullptr;
  }
  state->values.commit();
  if (times != nullptr) {
    times->createUs = static_cast<double>(created - start) / 1000.0;
    times->loadUs = static_cast<double>(loaded - created) / 1000.0;
    times->memory = state->context->memoryUsed();
  }
  return state;
}

void AudScriptRunner::adopt(std::unique_ptr<AudScriptState> state) {
  state_ = std::move(state);
  loadSendsPending_ = state_ != nullptr && !state_->loadSends.empty();
}

AudScriptTickResult AudScriptRunner::runTick(const AudScriptBlock& block,
                                             const AudScriptInput* inputs,
                                             size_t count, int64_t budgetNs) {
  AudScriptTickResult result;
  result.grid = audScriptGrid(block);
  AudScriptState* state = state_.get();
  if (state == nullptr) return result;
  result.ran = true;
  const int64_t start = aud_clock_now_ns();
  const int64_t deadline = start + (budgetNs > 0 ? budgetNs : 1);
  if (loadSendsPending_ && sendLog != nullptr) {
    sendLog->insert(sendLog->end(), state->loadSends.begin(),
                    state->loadSends.end());
    loadSendsPending_ = false;
  }
  const size_t tickStart = sendLog != nullptr ? sendLog->size() : 0;
  state->values.begin();
  std::string message;
  int32_t status = AUD_SCRIPT_TICK_OK;
  for (size_t i = 0; i < count && status != AUD_SCRIPT_TICK_INTERRUPTED; ++i) {
    const AudScriptInput& input = inputs[i];
    const int32_t delivered =
        input.kind == AudScriptInput::kControlChange
            ? state->context->controlChange(input.channel, input.number,
                                            input.value, deadline, &message)
            : state->context->control(input.number, input.value, deadline,
                                      &message);
    if (delivered == AUD_SCRIPT_TICK_INTERRUPTED) {
      status = delivered;
    } else if (delivered != AUD_SCRIPT_TICK_OK) {
      error(state->name, message);
    }
  }
  if (status == AUD_SCRIPT_TICK_OK) {
    AudScriptTime time;
    time.beat = result.grid.beat;
    time.tempo = block.tempo;
    time.playing = block.playing;
    time.sampleRate = block.sampleRate;
    time.frames = block.frames;
    time.tick = block.tick;
    status = state->context->tick(time, deadline, &message);
  }
  if (status == AUD_SCRIPT_TICK_OK) {
    state->values.commit();
  } else {
    state->values.rollback();
    // The sends of the load stay; those of the tick go.
    if (sendLog != nullptr) sendLog->resize(tickStart);
    error(state->name, status == AUD_SCRIPT_TICK_INTERRUPTED
                           ? "the tick ran over its budget and was "
                             "interrupted: " +
                                 message
                           : message);
  }
  result.status = status;
  result.timeUs = static_cast<double>(aud_clock_now_ns() - start) / 1000.0;
  return result;
}

}  // namespace aud_script
