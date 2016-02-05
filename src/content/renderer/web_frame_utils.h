// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_WEB_FRAME_UTILS_H_
#define CONTENT_RENDERER_WEB_FRAME_UTILS_H_

namespace blink {
class WebFrame;
}

namespace content {

// Returns the routing ID of the RenderFrameImpl or RenderFrameProxy
// associated with |web_frame|.
int GetRoutingIdForFrameOrProxy(blink::WebFrame* web_frame);

}  // namespace content

#endif  // CONTENT_RENDERER_WEB_FRAME_UTILS_H_
