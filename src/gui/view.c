#include "gui/view.h"
#include "gui/darkroom.h"
#include "gui/lighttable.h"

int
dt_view_switch(dt_gui_view_t view)
{
  int err = 0;
  switch(vkdt.view_mode)
  {
    case s_view_darkroom:
      err = darkroom_leave();
      break;
    default:;
  }
  if(err) return err;
  dt_gui_view_t old_view = vkdt.view_mode;
  vkdt.view_mode = view;
  switch(vkdt.view_mode)
  {
    case s_view_darkroom:
      err = darkroom_enter();
      break;
    default:;
  }
  // TODO: reshuffle this stuff so we don't have to re-enter the old view?
  if(err)
  {
    vkdt.view_mode = old_view;
    return err;
  }
  return 0;
}

void
dt_view_handle_event(SDL_Event *event)
{
  switch(vkdt.view_mode)
  {
  case s_view_darkroom:
    darkroom_handle_event(event);
    break;
  case s_view_lighttable:
    lighttable_handle_event(event);
    break;
  default:;
  }
}

void
dt_view_process()
{
  switch(vkdt.view_mode)
  {
    case s_view_darkroom:
      darkroom_process();
      break;
    default:;
  }
}
