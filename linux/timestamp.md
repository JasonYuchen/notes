# Timestamp

- [Pitfalls of TSC usage](https://oliveryang.net/2015/09/pitfalls-of-TSC-usage/)
- [How We Trace a KV Database with Less than 5% Performance Impact](https://www.pingcap.com/blog/how-we-trace-a-kv-database-with-less-than-5-percent-performance-impact/)
- [ministant](https://github.com/tikv/minstant)
- [Acquiring high-resolution time stamps in Windows](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)

## System call

- `gettimeofday()`: microsecond resolution
- `clock_gettime()`: nanosecond resolution
  - with `CLOCK_MONOTONIC_COARSE`, more performant ~10ns with relatively low precision ~4ms

The system call cost is mitigated by Virtual Dynamically Linked Shared Objects, VSDOs.

## Time Stamp Counter, TSC

- **performant**: single instruction `RDTSC` to get TSC, ~10ns
- **high resolution**: nanosecond
- **unreliable**
  - **constant?** some TSC could be impacted by CPU frequency change, check flag `constant_tsc` in `/proc/cpuinfo`
  - **non stop?** some TSC could be stopped by CPU in deep C-state (power saving), check flag `nonstop_tsc` in `/proc/cpuinfo`
  - **synchronized?** some TSC could be unsynchronized in SMP systems, do TSC sync testing or [manually offset adjustment](https://github.com/tikv/minstant/blob/27c9ec5ec90b5b67113a748a4defee0d2519518c/src/tsc_now.rs#L108), may also check flag `tsc_reliable` in `/proc/cpuinfo`

If TSC sync test passed during Linux kernel boot, TSC will be used as current clock source:

```shell
$ cat /sys/devices/system/clocksource/clocksource0/current_clocksource
tsc
```

TSC is considered safe if passed above tests

### Software Bugs

- TSC Calculation Overflow
- Incorrect CPU Frequency Usage
- Out-of-Order Execution Issues

### Hypervisors

- Native or pass-through: fast but potentially incorrect, VM could be migrated to different machine
- Emulated or Trap: correct but slow
- Hybrid: correct but potentially slow

## Conclusion

Avoid to use TSC in user applications
