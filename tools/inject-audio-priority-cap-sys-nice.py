#!/usr/bin/env python3
from pathlib import Path

p = Path("source/src/platform/linux/misc.cpp")
s = p.read_text()

anchor = '''    if (!success) {
      // This will run on FreeBSD OR Linux if RTKit failed/was missing
      if (setpriority(PRIO_PROCESS, 0, linux_nice) == -1) {
        BOOST_LOG(warning) << "setpriority failed for nice "sv << linux_nice << ": "sv << strerror(errno);
      } else {
        BOOST_LOG(debug) << "setpriority success for nice "sv << linux_nice;
      }
    }
'''

replacement = '''    if (!success) {
      // This will run on FreeBSD OR Linux if RTKit failed/was missing.
      // On Linux, Sunshine packages CAP_SYS_NICE in the permitted set and
      // normally keeps it out of the effective set. Temporarily enable it for
      // this calling thread so the setpriority() fallback can actually apply
      // the requested negative nice value, then restore the previous state.
#if !defined(__FreeBSD__)
      bool restore_sys_nice = false;
      cap_t caps = cap_get_proc();
      if (caps) {
        cap_value_t sys_nice = CAP_SYS_NICE;
        cap_flag_value_t permitted = CAP_CLEAR;
        cap_flag_value_t effective = CAP_CLEAR;

        if (cap_get_flag(caps, sys_nice, CAP_PERMITTED, &permitted) == 0 &&
            cap_get_flag(caps, sys_nice, CAP_EFFECTIVE, &effective) == 0 &&
            permitted == CAP_SET && effective == CAP_CLEAR) {
          if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &sys_nice, CAP_SET) == 0 && cap_set_proc(caps) == 0) {
            restore_sys_nice = true;
            BOOST_LOG(debug) << "CAP_SYS_NICE fallback: temporarily enabled for thread priority"sv;
          } else {
            BOOST_LOG(warning) << "CAP_SYS_NICE fallback: failed to enable effective capability: "sv << strerror(errno);
          }
        }
        cap_free(caps);
      }
#endif

      if (setpriority(PRIO_PROCESS, 0, linux_nice) == -1) {
        BOOST_LOG(warning) << "setpriority failed for nice "sv << linux_nice << ": "sv << strerror(errno);
      } else {
        BOOST_LOG(debug) << "setpriority success for nice "sv << linux_nice;
      }

#if !defined(__FreeBSD__)
      if (restore_sys_nice) {
        caps = cap_get_proc();
        if (caps) {
          cap_value_t sys_nice = CAP_SYS_NICE;
          if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &sys_nice, CAP_CLEAR) != 0 || cap_set_proc(caps) != 0) {
            BOOST_LOG(warning) << "CAP_SYS_NICE fallback: failed to restore effective capability state: "sv << strerror(errno);
          }
          cap_free(caps);
        } else {
          BOOST_LOG(warning) << "CAP_SYS_NICE fallback: failed to read capabilities while restoring state"sv;
        }
      }
#endif
    }
'''

count = s.count(anchor)
if count != 1:
    raise SystemExit(f"audio-priority anchor count={count}")

s = s.replace(anchor, replacement, 1)
p.write_text(s)
