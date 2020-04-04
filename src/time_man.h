#ifndef _OPENEVSE_TIME_H
#define _OPENEVSE_TIME_H

extern void time_begin(const char *host);
extern void time_loop();
extern void time_check_now();
extern void time_set_time(struct timeval set_time, const char *source);
extern String time_format_time(time_t time);
extern String time_format_time(tm &time);

#endif // _OPENEVSE_TIME_H
