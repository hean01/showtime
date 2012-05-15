/*
 *  Linux evdev interface
 *  Copyright (C) 2012 Henrik Andersson
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <linux/input.h>

#include "htsmsg/htsbuf.h"
#include "event.h"
#include "showtime.h"
#include "ui/ui.h"
#include "ipc/ipc.h"

#define IEV_MAX_DESCRIPTIORS 8

int liev_fd[IEV_MAX_DESCRIPTIORS + 1] = {0};

#define TEST_BIT(bit, array)  (array [bit / 8] & (1 << (bit % 8)))

static void *
liev_thread(void *aux)
{
  int ret, rd, i, *pfd;
  fd_set set;
  char buf[64];
  struct input_event ev[64];
  event_t *e;

  while(1) {  
    FD_ZERO(&set);
    for(pfd = liev_fd; *pfd != 0; pfd++)
      FD_SET(*pfd, &set);

    ret = select(FD_SETSIZE, &set, NULL, NULL, NULL);
    if(ret == -1)
      break;

    for(pfd = liev_fd; *pfd != 0; pfd++) 
    {
      if(!FD_ISSET(*pfd, &set))
	continue;
      
      rd = read(*pfd, ev, sizeof(struct input_event) * 64);
      if(rd < (int) sizeof(struct input_event)) {
	TRACE(TRACE_INFO, "liev", "Failed to read event from descriptor.");
	continue;
      }
	  
      for(i = 0; i < rd / sizeof(struct input_event); i++) {
	if(ev[i].type != EV_KEY || ev[i].value == 0)
	  continue;
	e = NULL;
	action_type_t av[3] = {ACTION_NONE};
	int c = ev[i].code;

	if     (c == KEY_UP)             av[0] = ACTION_UP;
	else if(c == KEY_DOWN)	         av[0] = ACTION_DOWN;
	else if(c == KEY_LEFT)	         av[0] = ACTION_LEFT;
	else if(c == KEY_RIGHT)	         av[0] = ACTION_RIGHT;
	else if(c == KEY_OK)	         av[0] = ACTION_ACTIVATE;
	else if(c == KEY_EXIT)	         av[0] = ACTION_NAV_BACK;

	else if(c == KEY_PLAYPAUSE)	 av[0] = ACTION_PLAYPAUSE;
	else if(c == KEY_STOP)	         av[0] = ACTION_STOP;
	else if(c == KEY_PAUSE)	         av[0] = ACTION_PAUSE;	
	else if(c == KEY_PLAY)	         av[0] = ACTION_PLAY;
	else if(c == KEY_RECORD)         av[0] = ACTION_RECORD;

	else if(c == KEY_NEXT ||
		c == KEY_NEXTSONG)       av[0] = ACTION_NEXT_TRACK;
	else if(c == KEY_PREVIOUS ||
		c == KEY_PREVIOUSSONG)	 av[0] = ACTION_PREV_TRACK;
	else if(c == KEY_SHUFFLE)	 av[0] = ACTION_SHUFFLE;
	
	else if(c == KEY_FORWARD)        av[0] = ACTION_SEEK_FAST_FORWARD;
	else if(c == KEY_REWIND)         av[0] = ACTION_SEEK_FAST_BACKWARD;

	else if(c == KEY_VOLUMEUP)	 av[0] = ACTION_VOLUME_UP;
	else if(c == KEY_VOLUMEDOWN)	 av[0] = ACTION_VOLUME_DOWN;	
	else if(c == KEY_CHANNELUP)      av[0] = ACTION_NEXT_CHANNEL;
	else if(c == KEY_CHANNELDOWN)    av[0] = ACTION_PREV_CHANNEL;

       

	if(av[0] != ACTION_NONE)
	  e = event_create_action_multi(av, 1);
	
	if(e == NULL) {
	  TRACE(TRACE_INFO, "liev", "Unknown event key %X", ev[i].code);
	  snprintf(buf, sizeof(buf), "LIEV+%X", ev[i].code);
	  e = event_create_str(EVENT_KEYDESC, buf);
	}

	ui_primary_event(e);
      }
    }
  }
  TRACE(TRACE_INFO, "liev", "Exiting event thread.");
  return NULL;
}

static void
liev_open()
{
  int fd, fdc, i, version;
  unsigned short id[4];
  char dev[64];
  char name[256] = "Unknown";
  u_int8_t evtype_bitmask[(EV_MAX + 7) / 8];

  fdc = 0;
  for(i = 0; i < 16; i++)
  {
    sprintf(dev, "/dev/input/event%d", i);
    fd = open(dev, O_RDONLY);
    if(fd < 0) {
      if(errno == EACCES)
	TRACE(TRACE_INFO, "liev", "Permission denied opening dev %s", dev);
      continue;
    }

    if (ioctl(fd, EVIOCGVERSION, &version))
      continue;
 
    ioctl(fd, EVIOCGID, id);
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    ioctl(fd, EVIOCGBIT(0, sizeof(evtype_bitmask)), evtype_bitmask);

    TRACE(TRACE_INFO, "liev", "Probing input types for device '%s'",
	  !strcmp(name, "Unknown") ? dev : name);

    if(TEST_BIT(EV_KEY, evtype_bitmask)) {
      TRACE(TRACE_INFO, "liev", "Listening on input events for device '%s'",
	    !strcmp(name, "Unknown") ? dev : name);
      liev_fd[fdc] = fd;      
      ++fdc;
      if(fdc == IEV_MAX_DESCRIPTIORS)
	break;
    }

  }

  if(fdc == 0) {
    TRACE(TRACE_INFO, "liev", "Failed to find any suitable input device");
    return;
  }

  hts_thread_create_detached("liev", liev_thread, NULL,
			     THREAD_PRIO_NORMAL);
  return;
}


/**
 *
 */
void
liev_start(void)
{
  liev_open();
}
