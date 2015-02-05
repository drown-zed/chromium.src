// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_APP_LIST_VIEWS_START_PAGE_VIEW_H_
#define UI_APP_LIST_VIEWS_START_PAGE_VIEW_H_

#include <vector>

#include "base/basictypes.h"
#include "ui/app_list/app_list_export.h"
#include "ui/views/view.h"

namespace app_list {

class AllAppsTileItemView;
class AppListMainView;
class AppListViewDelegate;
class SearchResultTileItemView;
class TileItemView;

// The start page for the experimental app list.
class APP_LIST_EXPORT StartPageView : public views::View {
 public:
  StartPageView(AppListMainView* app_list_main_view,
                AppListViewDelegate* view_delegate);
  ~StartPageView() override;

  void Reset();

  void UpdateForTesting();

  const std::vector<SearchResultTileItemView*>& tile_views() const;
  TileItemView* all_apps_button() const;

  // Called when the start page view is displayed.
  void OnShow();

  // Called when the start page view is hidden (while the app list is still
  // open).
  void OnHide();

  // Overridden from views::View:
  void Layout() override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;

  // Returns search box bounds to use when the start page is active.
  gfx::Rect GetSearchBoxBounds() const;

  // Updates whether the custom page clickzone is visible.
  void UpdateCustomPageClickzoneVisibility();

 private:
  class StartPageTilesContainer;

  void InitInstantContainer();

  TileItemView* GetTileItemView(size_t index);

  // The parent view of ContentsView which is the parent of this view.
  AppListMainView* app_list_main_view_;

  AppListViewDelegate* view_delegate_;  // Owned by AppListView.

  views::View* search_box_spacer_view_;  // Owned by views hierarchy.
  views::View* instant_container_;  // Owned by views hierarchy.
  StartPageTilesContainer* tiles_container_;  // Owned by views hierarchy.

  DISALLOW_COPY_AND_ASSIGN(StartPageView);
};

}  // namespace app_list

#endif  // UI_APP_LIST_VIEWS_START_PAGE_VIEW_H_
