#!/usr/bin/env python3
from pathlib import Path

TICKS_PER_MS = 25_000
SAVER_MS = 30_000
MASK64 = (1 << 64) - 1


def signed64(value):
    value &= MASK64
    return value - (1 << 64) if value & (1 << 63) else value


def deadline_reached(now, deadline):
    return signed64(now - deadline) >= 0


class Saver:
    def __init__(self, now=0):
        self.deadline = now + SAVER_MS * TICKS_PER_MS
        self.success_session = 0
        self.success_without_session = False
        self.reset_count = 0

    def reset(self, now):
        self.deadline = now + SAVER_MS * TICKS_PER_MS
        self.reset_count += 1

    def provision(self, now, success, session):
        activity = False
        if success and session and session != self.success_session:
            self.success_session = session
            activity = True
        elif success and not session and not self.success_without_session:
            self.success_without_session = True
            activity = True
        elif not success:
            self.success_without_session = False
        if activity:
            self.reset(now)
        return activity


ui = Path(__file__).with_name("airlink_ui.c").read_text(encoding="utf-8")
assert "static uint64_t saver_deadline;" in ui
assert "deadline_reached(now, saver_deadline)" in ui
assert "last_activity" not in ui
start = ui.index("static void provision_update")
end = ui.index("static uint32_t mode_controls_locked", start)
assert "ui_read_time()" not in ui[start:end]

saver = Saver(0)
assert not deadline_reached(29_999 * TICKS_PER_MS, saver.deadline)
assert deadline_reached(30_000 * TICKS_PER_MS, saver.deadline)

# A provisioning operation may take longer than the original inactivity
# window. SUCCESS starts a fresh, complete 30-second deadline.
success_at = 60_000 * TICKS_PER_MS
assert saver.provision(success_at, True, 7)
assert not deadline_reached(success_at + 29_999 * TICKS_PER_MS, saver.deadline)
assert deadline_reached(success_at + 30_000 * TICKS_PER_MS, saver.deadline)

# Repeated publication of the same SUCCESS session must not extend the timer.
saved_deadline = saver.deadline
assert not saver.provision(success_at + 5_000 * TICKS_PER_MS, True, 7)
assert saver.deadline == saved_deadline

# A new provisioning session resets it exactly once.
assert saver.provision(success_at + 10_000 * TICKS_PER_MS, True, 8)
assert saver.reset_count == 2
assert not saver.provision(success_at + 11_000 * TICKS_PER_MS, True, 8)
assert saver.reset_count == 2

# Signed deadline comparison remains safe across the 64-bit counter wrap.
near_wrap = MASK64 - 10 * TICKS_PER_MS
wrapped_deadline = (near_wrap + 30 * TICKS_PER_MS) & MASK64
assert not deadline_reached((near_wrap + 20 * TICKS_PER_MS) & MASK64,
                            wrapped_deadline)
assert deadline_reached((near_wrap + 30 * TICKS_PER_MS) & MASK64,
                        wrapped_deadline)

print("R27.2.9 30-second saver deadline/session tests: PASS")
