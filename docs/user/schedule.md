# Schedule

Recurring charge timers — charge on cheap overnight tariffs, pre-heat before
the commute, or block charging during peak hours.

![Schedule screen](screenshots/schedule-dark-desktop.png)

- Each timer has a time, a state to apply (active/disabled), and the weekdays
  it applies to; up to 50 timers are supported.
- **+ New timer** adds one; the pencil edits and the bin deletes.
- Timers act at the **Timer/Scheduler** priority: a manual override from the
  [Dashboard](dashboard.md) (On/Off/Boost) wins until it is released, after
  which the schedule resumes control.
- The device evaluates the schedule on-board — timers keep working if your
  WiFi or internet is down (time is kept via SNTP; set your timezone under
  [Settings → Time & Date](settings.md#time--date)).

A common pattern is a pair of timers: *active* at the start of your off-peak
window, *disabled* at the end of it, on all seven days.
