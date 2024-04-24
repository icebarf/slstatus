#include "slstatus.h"
#include "util.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEP(sep) ((struct arg){separator, sep, NULL})
#define SEP_DEFAULT (" | ")

enum { IBUF_SIZE = 128 };

static const char *const SEPERATOR = " | ";
static const char *const BAT0 = "BAT0";
static const char *const BAT1 = "BAT1"; // optional

static bool is_iface_wireless(char ibuf[IBUF_SIZE]) {
  char path[PATH_MAX] = {0};

  if (snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", ibuf) == -1) {
    die("snprintf:");
  }

  DIR *dir = opendir(path);

  if (!dir) {
    if (errno == ENOENT) {
      errno = 0;
      return false;
    }

    die("opendir:");
  }

  closedir(dir);

  return true;
}

static int get_default_iface(char ibuf[IBUF_SIZE]) {
  FILE *fp = popen("ip route get 255.255.255.255", "r");

  if (!fp) {
    die("popen:");
  }

  char buf[1024] = {0};

  errno = 0;

  if (!fgets(buf, sizeof(buf), fp) && errno != 0) {
    die("fgets:");
  }

  while (fgetc(fp) != EOF) {
    /* Drain the pipe */
  }

  int ret = pclose(fp);

  if (ret == -1) {
    die("pclose:");
  }

  if (ret != 0) {
    return -1;
  }

  char *s = buf;

  for (size_t space_count = 0;;) {
    s = strchr(s, ' ');

    if (!s) {
      return -1;
    }

    s++;

    /* broadcast 255.255.255.255 dev usb0 src ... */
    if (++space_count == 3) {
      char *end = strchr(s, ' ');

      if (!end) {
        return -1;
      }

      if (snprintf(ibuf, IBUF_SIZE, "%.*s", (int)(end - s), s) == -1) {
        die("snprintf:");
      }

      return 0;
    }
  }

  return -1;
}

__attribute__((format(printf, 4, 5))) static size_t
append_sep(char *restrict s, size_t n, size_t nul, const char *restrict format,
           ...) {
  if (*s != '\0') {
    int ret = snprintf(s + nul, n - nul, "%s", SEPERATOR);

    if (ret == -1) {
      die("snprintf:");
    }

    nul += ret;
  }

  va_list ap;
  va_start(ap, format);

  int ret = vsnprintf(s + nul, n - nul, format, ap);

  if (ret == -1) {
    die("vsnprintf:");
  }

  return nul + ret;
}

/* Hacky wrapper to use upto 5 arguments in a single append statement
 * s comes from a static buffer that gets overwritten */
static const char *wrap(const char *s) {
  enum { BUFS = 5, BUFLEN = 128 };

  static size_t bufi = 0;
  static char bufs[BUFS][BUFLEN] = {0};

  if (s) {
    char *buf = bufs[bufi++ % BUFS];

    if (snprintf(buf, BUFLEN, "%s", s) == -1) {
      die("snprintf:");
    }

    return buf;
  }

  return unknown_str;
}

static const char *state_to_str(const char state) {
  switch (state) {
  case 'o':
    return "Charged";
  case '-':
    return "Discharging";
  case '+':
    return "Charging";
  }

  errno = EINVAL;
  die("state_to_str:");
}

static const char *battery_icon(const char *const perc) {
  enum state {
    EMPTY,
    FULL,
    QUARTER,
    HALF,
    THREE_QUARTERS,
    STATE_MAX,
  };

  const char *const state_to_icon[STATE_MAX] = {
      [EMPTY] = "", [FULL] = "",           [QUARTER] = "",
      [HALF] = "",  [THREE_QUARTERS] = "",
  };

  unsigned long ul_perc = strtoul(perc, NULL, 10);

  int diffs[STATE_MAX] = {
      [EMPTY] = ul_perc - 0,           [FULL] = ul_perc - 100,
      [QUARTER] = ul_perc - 25,        [HALF] = ul_perc - 50,
      [THREE_QUARTERS] = ul_perc - 75,
  };

  enum state closest_state = EMPTY;

  for (size_t i = FULL; i < STATE_MAX; i++) {
    if (abs(diffs[i]) < abs(diffs[closest_state])) {
      closest_state = i;
    }
  }

  return state_to_icon[closest_state];
}

void get_status(char status[MAXLEN]) {
  static char ibuf[IBUF_SIZE];
  size_t nulpos = 0;

  if (get_default_iface(ibuf) == -1) {
    warn("get_default_iface failed");
  } else {
    if (is_iface_wireless(ibuf)) {
      nulpos = append_sep(status, MAXLEN, nulpos, " %s (%s / %s)",
                          wrap(wifi_essid(ibuf)), wrap(netspeed_rx(ibuf)),
                          wrap(netspeed_tx(ibuf)));
    } else {
      nulpos = append_sep(status, MAXLEN, nulpos, " %s (%s / %s)", ibuf,
                          wrap(netspeed_rx(ibuf)), wrap(netspeed_tx(ibuf)));
    }
  }

  const char *const remaining0 = wrap(battery_remaining(BAT0));
  const char *const state0 = wrap(battery_state(BAT0));
  const char *const perc0 = wrap(battery_perc(BAT0));
  
  nulpos = append_sep(status, MAXLEN, nulpos, "%s %s%% (%s%s%s)",
                      battery_icon(perc0), perc0, state_to_str(*state0),
                      strlen(remaining0) ? ", " : "", remaining0);

  bool other_battery_exists = dir_exists("/sys/class/power_supply/BAT1");

  if (other_battery_exists) {
    const char *const remaining1 = wrap(battery_remaining(BAT1));
    const char *const state1 = wrap(battery_state(BAT1));
    const char *const perc1 = wrap(battery_perc(BAT1));
    nulpos = append_sep(status, MAXLEN, nulpos, "%s %s%% (%s%s%s)",
                        battery_icon(perc1), perc1, state_to_str(*state1),
                        strlen(remaining1) ? ", " : "", remaining1);
  }

  nulpos = append_sep(status, MAXLEN, nulpos, " %s%%", wrap(cpu_perc(NULL)));
  nulpos = append_sep(status, MAXLEN, nulpos, " %s%% (%s%%)",
                      wrap(ram_perc(NULL)), wrap(swap_perc(NULL)));
  nulpos =
      append_sep(status, MAXLEN, nulpos, "%s", wrap(datetime(DATETIME_FORMAT)));
}
