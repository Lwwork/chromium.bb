// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SCHEDULER_RENDERER_WEB_VIEW_SCHEDULER_IMPL_H_
#define COMPONENTS_SCHEDULER_RENDERER_WEB_VIEW_SCHEDULER_IMPL_H_

#include <set>

#include "base/macros.h"
#include "components/scheduler/scheduler_export.h"
#include "third_party/WebKit/public/platform/WebViewScheduler.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace blink {
class WebView;
}  // namespace blink

namespace scheduler {

class RendererSchedulerImpl;
class WebFrameSchedulerImpl;

class SCHEDULER_EXPORT WebViewSchedulerImpl : public blink::WebViewScheduler {
 public:
  WebViewSchedulerImpl(blink::WebView* web_view,
                       RendererSchedulerImpl* renderer_scheduler);

  ~WebViewSchedulerImpl() override;

  // blink::WebViewScheduler implementation:
  void setPageInBackground(bool background) override;
  blink::WebPassOwnPtr<blink::WebFrameScheduler> createFrameScheduler()
      override;

  blink::WebView* web_view() const { return web_view_; }

 private:
  friend class WebFrameSchedulerImpl;

  void Unregister(WebFrameSchedulerImpl* frame_scheduler);

  std::set<WebFrameSchedulerImpl*> frame_schedulers_;
  blink::WebView* web_view_;
  RendererSchedulerImpl* renderer_scheduler_;
  bool background_;

  DISALLOW_COPY_AND_ASSIGN(WebViewSchedulerImpl);
};

}  // namespace scheduler

#endif  // COMPONENTS_SCHEDULER_RENDERER_WEB_VIEW_SCHEDULER_IMPL_H_
