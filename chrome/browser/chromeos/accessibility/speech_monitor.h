// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_ACCESSIBILITY_SPEECH_MONITOR_H_
#define CHROME_BROWSER_CHROMEOS_ACCESSIBILITY_SPEECH_MONITOR_H_

#include <deque>

#include "base/memory/ref_counted.h"
#include "chrome/browser/speech/tts_platform.h"
#include "content/public/test/test_utils.h"

namespace chromeos {

// For testing purpose installs itself as the platform speech synthesis engine,
// allowing it to intercept all speech calls, and then provides a method to
// block until the next utterance is spoken.
class SpeechMonitor : public TtsPlatformImpl {
 public:
  SpeechMonitor();
  virtual ~SpeechMonitor();

  // Blocks until the next utterance is spoken, and returns its text.
  std::string GetNextUtterance();

  // Wait for next utterance and return true if next utterance is ChromeVox
  // enabled message.
  bool SkipChromeVoxEnabledMessage();

  // TtsPlatformImpl implementation.
  virtual bool PlatformImplAvailable() override;
  virtual bool Speak(
      int utterance_id,
      const std::string& utterance,
      const std::string& lang,
      const VoiceData& voice,
      const UtteranceContinuousParameters& params) override;
  virtual bool StopSpeaking() override;
  virtual bool IsSpeaking() override;
  virtual void GetVoices(std::vector<VoiceData>* out_voices) override;
  virtual void Pause() override {}
  virtual void Resume() override {}
  virtual std::string error() override;
  virtual void clear_error() override {}
  virtual void set_error(const std::string& error) override {}
  virtual void WillSpeakUtteranceWithVoice(
      const Utterance* utterance, const VoiceData& voice_data) override;

 private:
  scoped_refptr<content::MessageLoopRunner> loop_runner_;
  std::deque<std::string> utterance_queue_;

  DISALLOW_COPY_AND_ASSIGN(SpeechMonitor);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_ACCESSIBILITY_SPEECH_MONITOR_H_
