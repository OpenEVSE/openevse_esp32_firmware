#ifndef _OPENEVSE_SNTP_H
#define _OPENEVSE_SNTP_H

extern void sntp_begin(const char *host);
extern void sntp_loop();
extern void sntp_check_now();
extern void sntp_set_time(struct timeval set_time, const char *source);
extern String sntp_format_time(time_t time);
extern String sntp_format_time(tm &time);

#endif // _OPENEVSE_SNTP_H
