// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_ANDROID_WINDOW_ANDROID_OBSERVER_H_
#define UI_ANDROID_WINDOW_ANDROID_OBSERVER_H_

#include "ui/android/ui_android_export.h"

namespace ui {

class UI_ANDROID_EXPORT WindowAndroidObserver {
 public:
  virtual void OnCompositingDidCommit() = 0;
  virtual void OnVisibilityChanged(bool visible) = 0;
  virtual void OnAttachCompositor() = 0;
  virtual void OnDetachCompositor() = 0;
  virtual void OnVSync(base::TimeTicks frame_time,
                       base::TimeDelta vsync_period) = 0;
  virtual void OnAnimate(base::TimeTicks frame_begin_time) {}
  virtual void OnActivityPaused() = 0;
  virtual void OnActivityResumed() = 0;

 protected:
  virtual ~WindowAndroidObserver() {}
};

}  // namespace ui

#endif  // UI_ANDROID_WINDOW_ANDROID_OBSERVER_H_
