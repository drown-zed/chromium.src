// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <X11/extensions/shape.h>
#include <X11/Xlib.h>

// Get rid of X11 macros which conflict with gtest.
// It is necessary to include this header before the rest so that Bool can be
// undefined.
#include "ui/events/test/events_test_utils_x11.h"
#undef Bool
#undef None

#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/run_loop.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/hit_test.h"
#include "ui/base/x/x11_util.h"
#include "ui/events/devices/x11/touch_factory_x11.h"
#include "ui/events/platform/x11/x11_event_source.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/path.h"
#include "ui/gfx/switches.h"
#include "ui/gfx/x/x11_atom_cache.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/x11_property_change_waiter.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_x11.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/non_client_view.h"

namespace views {

namespace {

const int kPointerDeviceId = 1;

// Blocks till the window state hint, |hint|, is set or unset.
class WMStateWaiter : public X11PropertyChangeWaiter {
 public:
  WMStateWaiter(XID window,
                const char* hint,
                bool wait_till_set)
      : X11PropertyChangeWaiter(window, "_NET_WM_STATE"),
        hint_(hint),
        wait_till_set_(wait_till_set) {

    const char* kAtomsToCache[] = {
        hint,
        NULL
    };
    atom_cache_.reset(new ui::X11AtomCache(gfx::GetXDisplay(), kAtomsToCache));
  }

  ~WMStateWaiter() override {}

 private:
  // X11PropertyChangeWaiter:
  bool ShouldKeepOnWaiting(const ui::PlatformEvent& event) override {
    std::vector<Atom> hints;
    if (ui::GetAtomArrayProperty(xwindow(), "_NET_WM_STATE", &hints)) {
      std::vector<Atom>::iterator it = std::find(
          hints.begin(),
          hints.end(),
          atom_cache_->GetAtom(hint_));
      bool hint_set = (it != hints.end());
      return hint_set != wait_till_set_;
    }
    return true;
  }

  scoped_ptr<ui::X11AtomCache> atom_cache_;

  // The name of the hint to wait to get set or unset.
  const char* hint_;

  // Whether we are waiting for |hint| to be set or unset.
  bool wait_till_set_;

  DISALLOW_COPY_AND_ASSIGN(WMStateWaiter);
};

// A NonClientFrameView with a window mask with the bottom right corner cut out.
class ShapedNonClientFrameView : public NonClientFrameView {
 public:
  explicit ShapedNonClientFrameView() {
  }

  ~ShapedNonClientFrameView() override {}

  // NonClientFrameView:
  gfx::Rect GetBoundsForClientView() const override { return bounds(); }
  gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const override {
    return client_bounds;
  }
  int NonClientHitTest(const gfx::Point& point) override { return HTNOWHERE; }
  void GetWindowMask(const gfx::Size& size, gfx::Path* window_mask) override {
    int right = size.width();
    int bottom = size.height();

    window_mask->moveTo(0, 0);
    window_mask->lineTo(0, bottom);
    window_mask->lineTo(right, bottom);
    window_mask->lineTo(right, 10);
    window_mask->lineTo(right - 10, 10);
    window_mask->lineTo(right - 10, 0);
    window_mask->close();
  }
  void ResetWindowControls() override {}
  void UpdateWindowIcon() override {}
  void UpdateWindowTitle() override {}
  void SizeConstraintsChanged() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ShapedNonClientFrameView);
};

class ShapedWidgetDelegate : public WidgetDelegateView {
 public:
  ShapedWidgetDelegate() {
  }

  ~ShapedWidgetDelegate() override {}

  // WidgetDelegateView:
  NonClientFrameView* CreateNonClientFrameView(Widget* widget) override {
    return new ShapedNonClientFrameView;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ShapedWidgetDelegate);
};

// Creates a widget of size 100x100.
scoped_ptr<Widget> CreateWidget(WidgetDelegate* delegate) {
  scoped_ptr<Widget> widget(new Widget);
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.delegate = delegate;
  params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.remove_standard_frame = true;
  params.native_widget = new DesktopNativeWidgetAura(widget.get());
  params.bounds = gfx::Rect(100, 100, 100, 100);
  widget->Init(params);
  return widget.Pass();
}

// Returns the list of rectangles which describe |xid|'s bounding region via the
// X shape extension.
std::vector<gfx::Rect> GetShapeRects(XID xid) {
  int dummy;
  int shape_rects_size;
  gfx::XScopedPtr<XRectangle[]> shape_rects(XShapeGetRectangles(
      gfx::GetXDisplay(), xid, ShapeBounding, &shape_rects_size, &dummy));

  std::vector<gfx::Rect> shape_vector;
  for (int i = 0; i < shape_rects_size; ++i) {
    const XRectangle& rect = shape_rects[i];
    shape_vector.push_back(gfx::Rect(rect.x, rect.y, rect.width, rect.height));
  }
  return shape_vector;
}

// Returns true if one of |rects| contains point (x,y).
bool ShapeRectContainsPoint(const std::vector<gfx::Rect>& shape_rects,
                            int x,
                            int y) {
  gfx::Point point(x, y);
  for (size_t i = 0; i < shape_rects.size(); ++i) {
    if (shape_rects[i].Contains(point))
      return true;
  }
  return false;
}

// Flush the message loop.
void RunAllPendingInMessageLoop() {
  base::RunLoop run_loop;
  run_loop.RunUntilIdle();
}

}  // namespace

class DesktopWindowTreeHostX11Test : public ViewsTestBase {
 public:
  DesktopWindowTreeHostX11Test() {
  }
  ~DesktopWindowTreeHostX11Test() override {}

  void SetUp() override {
    ViewsTestBase::SetUp();

    // Make X11 synchronous for our display connection. This does not force the
    // window manager to behave synchronously.
    XSynchronize(gfx::GetXDisplay(), True);
  }

  void TearDown() override {
    XSynchronize(gfx::GetXDisplay(), False);
    ViewsTestBase::TearDown();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DesktopWindowTreeHostX11Test);
};

// Tests that the shape is properly set on the x window.
TEST_F(DesktopWindowTreeHostX11Test, Shape) {
  if (!ui::IsShapeExtensionAvailable())
    return;

  // 1) Test setting the window shape via the NonClientFrameView. This technique
  // is used to get rounded corners on Chrome windows when not using the native
  // window frame.
  scoped_ptr<Widget> widget1 = CreateWidget(new ShapedWidgetDelegate());
  widget1->Show();
  ui::X11EventSource::GetInstance()->DispatchXEvents();

  XID xid1 = widget1->GetNativeWindow()->GetHost()->GetAcceleratedWidget();
  std::vector<gfx::Rect> shape_rects = GetShapeRects(xid1);
  ASSERT_FALSE(shape_rects.empty());

  // The widget was supposed to be 100x100, but the WM might have ignored this
  // suggestion.
  int widget_width = widget1->GetWindowBoundsInScreen().width();
  EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, widget_width - 15, 5));
  EXPECT_FALSE(ShapeRectContainsPoint(shape_rects, widget_width - 5, 5));
  EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, widget_width - 5, 15));
  EXPECT_FALSE(ShapeRectContainsPoint(shape_rects, widget_width + 5, 15));

  // Changing widget's size should update the shape.
  widget1->SetBounds(gfx::Rect(100, 100, 200, 200));
  ui::X11EventSource::GetInstance()->DispatchXEvents();

  if (widget1->GetWindowBoundsInScreen().width() == 200) {
    shape_rects = GetShapeRects(xid1);
    ASSERT_FALSE(shape_rects.empty());
    EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 85, 5));
    EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 95, 5));
    EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 185, 5));
    EXPECT_FALSE(ShapeRectContainsPoint(shape_rects, 195, 5));
    EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 195, 15));
    EXPECT_FALSE(ShapeRectContainsPoint(shape_rects, 205, 15));
  }

  if (ui::WmSupportsHint(ui::GetAtom("_NET_WM_STATE_MAXIMIZED_VERT"))) {
    // The shape should be changed to a rectangle which fills the entire screen
    // when |widget1| is maximized.
    {
      WMStateWaiter waiter(xid1, "_NET_WM_STATE_MAXIMIZED_VERT", true);
      widget1->Maximize();
      waiter.Wait();
    }

    // Ensure that the task which is posted when a window is resized is run.
    RunAllPendingInMessageLoop();

    // xvfb does not support Xrandr so we cannot check the maximized window's
    // bounds.
    gfx::Rect maximized_bounds;
    ui::GetOuterWindowBounds(xid1, &maximized_bounds);

    shape_rects = GetShapeRects(xid1);
    ASSERT_FALSE(shape_rects.empty());
    EXPECT_TRUE(ShapeRectContainsPoint(shape_rects,
                                       maximized_bounds.width() - 1,
                                       5));
    EXPECT_TRUE(ShapeRectContainsPoint(shape_rects,
                                       maximized_bounds.width() - 1,
                                       15));
  }

  // 2) Test setting the window shape via Widget::SetShape().
  gfx::Path shape2;
  shape2.moveTo(10, 0);
  shape2.lineTo(10, 10);
  shape2.lineTo(0, 10);
  shape2.lineTo(0, 100);
  shape2.lineTo(100, 100);
  shape2.lineTo(100, 0);
  shape2.close();

  scoped_ptr<Widget> widget2(CreateWidget(NULL));
  widget2->Show();
  widget2->SetShape(shape2.CreateNativeRegion());
  ui::X11EventSource::GetInstance()->DispatchXEvents();

  XID xid2 = widget2->GetNativeWindow()->GetHost()->GetAcceleratedWidget();
  shape_rects = GetShapeRects(xid2);
  ASSERT_FALSE(shape_rects.empty());
  EXPECT_FALSE(ShapeRectContainsPoint(shape_rects, 5, 5));
  EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 15, 5));
  EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 95, 15));
  EXPECT_FALSE(ShapeRectContainsPoint(shape_rects, 105, 15));

  // Changing the widget's size should not affect the shape.
  widget2->SetBounds(gfx::Rect(100, 100, 200, 200));
  shape_rects = GetShapeRects(xid2);
  ASSERT_FALSE(shape_rects.empty());
  EXPECT_FALSE(ShapeRectContainsPoint(shape_rects, 5, 5));
  EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 15, 5));
  EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 95, 15));
  EXPECT_FALSE(ShapeRectContainsPoint(shape_rects, 105, 15));

  // Setting the shape to NULL resets the shape back to the entire
  // window bounds.
  widget2->SetShape(NULL);
  shape_rects = GetShapeRects(xid2);
  ASSERT_FALSE(shape_rects.empty());
  EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 5, 5));
  EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 15, 5));
  EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 95, 15));
  EXPECT_TRUE(ShapeRectContainsPoint(shape_rects, 105, 15));
  EXPECT_FALSE(ShapeRectContainsPoint(shape_rects, 500, 500));
}

// Test that the widget ignores changes in fullscreen state initiated by the
// window manager (e.g. via a window manager accelerator key).
TEST_F(DesktopWindowTreeHostX11Test, WindowManagerTogglesFullscreen) {
  if (!ui::WmSupportsHint(ui::GetAtom("_NET_WM_STATE_FULLSCREEN")))
    return;

  scoped_ptr<Widget> widget = CreateWidget(new ShapedWidgetDelegate());
  XID xid = widget->GetNativeWindow()->GetHost()->GetAcceleratedWidget();
  widget->Show();
  ui::X11EventSource::GetInstance()->DispatchXEvents();

  gfx::Rect initial_bounds = widget->GetWindowBoundsInScreen();
  {
    WMStateWaiter waiter(xid, "_NET_WM_STATE_FULLSCREEN", true);
    widget->SetFullscreen(true);
    waiter.Wait();
  }
  EXPECT_TRUE(widget->IsFullscreen());

  // Emulate the window manager exiting fullscreen via a window manager
  // accelerator key. It should not affect the widget's fullscreen state.
  {
    const char* kAtomsToCache[] = {
        "_NET_WM_STATE",
        "_NET_WM_STATE_FULLSCREEN",
        NULL
    };
    Display* display = gfx::GetXDisplay();
    ui::X11AtomCache atom_cache(display, kAtomsToCache);

    XEvent xclient;
    memset(&xclient, 0, sizeof(xclient));
    xclient.type = ClientMessage;
    xclient.xclient.window = xid;
    xclient.xclient.message_type = atom_cache.GetAtom("_NET_WM_STATE");
    xclient.xclient.format = 32;
    xclient.xclient.data.l[0] = 0;
    xclient.xclient.data.l[1] = atom_cache.GetAtom("_NET_WM_STATE_FULLSCREEN");
    xclient.xclient.data.l[2] = 0;
    xclient.xclient.data.l[3] = 1;
    xclient.xclient.data.l[4] = 0;
    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask,
               &xclient);

    WMStateWaiter waiter(xid, "_NET_WM_STATE_FULLSCREEN", false);
    waiter.Wait();
  }
  EXPECT_TRUE(widget->IsFullscreen());

  // Calling Widget::SetFullscreen(false) should clear the widget's fullscreen
  // state and clean things up.
  widget->SetFullscreen(false);
  EXPECT_FALSE(widget->IsFullscreen());
  EXPECT_EQ(initial_bounds.ToString(),
            widget->GetWindowBoundsInScreen().ToString());
}

// Tests that the minimization information is propagated to the content window.
TEST_F(DesktopWindowTreeHostX11Test, ToggleMinimizePropogateToContentWindow) {
  Widget widget;
  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_WINDOW);
  params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.native_widget = new DesktopNativeWidgetAura(&widget);
  widget.Init(params);
  widget.Show();
  ui::X11EventSource::GetInstance()->DispatchXEvents();

  XID xid = widget.GetNativeWindow()->GetHost()->GetAcceleratedWidget();
  Display* display = gfx::GetXDisplay();

  // Minimize by sending _NET_WM_STATE_HIDDEN
  {
    const char* kAtomsToCache[] = {
        "_NET_WM_STATE",
        "_NET_WM_STATE_HIDDEN",
        NULL
    };

    ui::X11AtomCache atom_cache(display, kAtomsToCache);

    std::vector< ::Atom> atom_list;
    atom_list.push_back(atom_cache.GetAtom("_NET_WM_STATE_HIDDEN"));
    ui::SetAtomArrayProperty(xid, "_NET_WM_STATE", "ATOM", atom_list);

    XEvent xevent;
    memset(&xevent, 0, sizeof(xevent));
    xevent.type = PropertyNotify;
    xevent.xproperty.type = PropertyNotify;
    xevent.xproperty.send_event = 1;
    xevent.xproperty.display = display;
    xevent.xproperty.window = xid;
    xevent.xproperty.atom = atom_cache.GetAtom("_NET_WM_STATE");
    xevent.xproperty.state = 0;
    XSendEvent(display, DefaultRootWindow(display), False,
        SubstructureRedirectMask | SubstructureNotifyMask,
        &xevent);

    WMStateWaiter waiter(xid, "_NET_WM_STATE_HIDDEN", true);
    waiter.Wait();
  }
  EXPECT_FALSE(widget.GetNativeWindow()->IsVisible());

  // Show from minimized by sending _NET_WM_STATE_FOCUSED
  {
    const char* kAtomsToCache[] = {
        "_NET_WM_STATE",
        "_NET_WM_STATE_FOCUSED",
        NULL
    };

    ui::X11AtomCache atom_cache(display, kAtomsToCache);

    std::vector< ::Atom> atom_list;
    atom_list.push_back(atom_cache.GetAtom("_NET_WM_STATE_FOCUSED"));
    ui::SetAtomArrayProperty(xid, "_NET_WM_STATE", "ATOM", atom_list);

    XEvent xevent;
    memset(&xevent, 0, sizeof(xevent));
    xevent.type = PropertyNotify;
    xevent.xproperty.type = PropertyNotify;
    xevent.xproperty.send_event = 1;
    xevent.xproperty.display = display;
    xevent.xproperty.window = xid;
    xevent.xproperty.atom = atom_cache.GetAtom("_NET_WM_STATE");
    xevent.xproperty.state = 0;
    XSendEvent(display, DefaultRootWindow(display), False,
        SubstructureRedirectMask | SubstructureNotifyMask,
        &xevent);

    WMStateWaiter waiter(xid, "_NET_WM_STATE_FOCUSED", true);
    waiter.Wait();
  }
  EXPECT_TRUE(widget.GetNativeWindow()->IsVisible());
}

class MouseEventRecorder : public ui::EventHandler {
 public:
  MouseEventRecorder() {}
  ~MouseEventRecorder() override {}

  void Reset() { mouse_events_.clear(); }

  const std::vector<ui::MouseEvent>& mouse_events() const {
    return mouse_events_;
  }

 private:
  // ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* mouse) override {
    mouse_events_.push_back(*mouse);
  }

  std::vector<ui::MouseEvent> mouse_events_;

  DISALLOW_COPY_AND_ASSIGN(MouseEventRecorder);
};

// A custom event-source that can be used to directly dispatch synthetic X11
// events.
class CustomX11EventSource : public ui::X11EventSource {
 public:
  CustomX11EventSource() : X11EventSource(gfx::GetXDisplay()) {}
  ~CustomX11EventSource() override {}

  void DispatchSingleEvent(XEvent* xevent) {
    PlatformEventSource::DispatchEvent(xevent);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CustomX11EventSource);
};

class DesktopWindowTreeHostX11HighDPITest
    : public DesktopWindowTreeHostX11Test {
 public:
  DesktopWindowTreeHostX11HighDPITest() {}
  ~DesktopWindowTreeHostX11HighDPITest() override {}

  void DispatchSingleEventToWidget(XEvent* event, Widget* widget) {
    DCHECK_EQ(GenericEvent, event->type);
    XIDeviceEvent* device_event =
        static_cast<XIDeviceEvent*>(event->xcookie.data);
    device_event->event =
        widget->GetNativeWindow()->GetHost()->GetAcceleratedWidget();
    event_source_.DispatchSingleEvent(event);
  }

  void PretendCapture(views::Widget* capture_widget) {
    DesktopWindowTreeHostX11* capture_host = nullptr;
    if (capture_widget) {
      capture_host = static_cast<DesktopWindowTreeHostX11*>(
          capture_widget->GetNativeWindow()->GetHost());
    }
    DesktopWindowTreeHostX11::g_current_capture = capture_host;
    if (capture_widget)
      capture_widget->GetNativeWindow()->SetCapture();
  }

 private:
  void SetUp() override {
    base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
    command_line->AppendSwitchASCII(switches::kForceDeviceScaleFactor, "2");
    std::vector<int> pointer_devices;
    pointer_devices.push_back(kPointerDeviceId);
    ui::TouchFactory::GetInstance()->SetPointerDeviceForTest(pointer_devices);

    DesktopWindowTreeHostX11Test::SetUp();
  }

  CustomX11EventSource event_source_;
  DISALLOW_COPY_AND_ASSIGN(DesktopWindowTreeHostX11HighDPITest);
};

TEST_F(DesktopWindowTreeHostX11HighDPITest, LocatedEventDispatchWithCapture) {
  Widget first;
  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_WINDOW);
  params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.native_widget = new DesktopNativeWidgetAura(&first);
  params.bounds = gfx::Rect(0, 0, 50, 50);
  first.Init(params);
  first.Show();

  Widget second;
  params = CreateParams(Widget::InitParams::TYPE_WINDOW);
  params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.native_widget = new DesktopNativeWidgetAura(&second);
  params.bounds = gfx::Rect(50, 50, 50, 50);
  second.Init(params);
  second.Show();

  ui::X11EventSource::GetInstance()->DispatchXEvents();

  MouseEventRecorder first_recorder, second_recorder;
  first.GetNativeWindow()->AddPreTargetHandler(&first_recorder);
  second.GetNativeWindow()->AddPreTargetHandler(&second_recorder);

  // Dispatch an event on |first|. Verify it gets the event.
  ui::ScopedXI2Event event;
  event.InitGenericButtonEvent(kPointerDeviceId, ui::ET_MOUSEWHEEL,
                               gfx::Point(50, 50), ui::EF_NONE);
  DispatchSingleEventToWidget(event, &first);
  ASSERT_EQ(1u, first_recorder.mouse_events().size());
  EXPECT_EQ(ui::ET_MOUSEWHEEL, first_recorder.mouse_events()[0].type());
  EXPECT_EQ(gfx::Point(25, 25).ToString(),
            first_recorder.mouse_events()[0].location().ToString());
  ASSERT_EQ(0u, second_recorder.mouse_events().size());

  first_recorder.Reset();
  second_recorder.Reset();

  // Set a capture on |second|, and dispatch the same event to |first|. This
  // event should reach |second| instead.
  PretendCapture(&second);
  event.InitGenericButtonEvent(kPointerDeviceId, ui::ET_MOUSEWHEEL,
                               gfx::Point(50, 50), ui::EF_NONE);
  DispatchSingleEventToWidget(event, &first);

  ASSERT_EQ(0u, first_recorder.mouse_events().size());
  ASSERT_EQ(1u, second_recorder.mouse_events().size());
  EXPECT_EQ(ui::ET_MOUSEWHEEL, second_recorder.mouse_events()[0].type());
  EXPECT_EQ(gfx::Point(-25, -25).ToString(),
            second_recorder.mouse_events()[0].location().ToString());

  PretendCapture(nullptr);
  first.GetNativeWindow()->RemovePreTargetHandler(&first_recorder);
  second.GetNativeWindow()->RemovePreTargetHandler(&second_recorder);
}

}  // namespace views
