# AirLink R27.6.6.23 CH347 switch race fix

This C906L-only revision is based on the validated R27.6.6.22 image and
replaces only BLCP_2ND.

Root cause:

- CH347 mode switching intentionally holds RESET# low for 80 ms.
- The one-second GPIO ownership monitor still ran during that pulse.
- If both overlapped, the monitor treated the intentional low reset as an
  ownership mismatch, restored the old current_mode, and raced the pending
  switch. The later readback then reported a false switch failure.

Fix:

- mark CH347 unavailable to the ownership monitor before asserting RESET#;
- skip periodic ownership checks while the local switch state machine is
  active;
- add a defensive guard inside airlink_ch347_check_ownership;
- after releasing RESET#, automatically re-assert the complete pending pin
  configuration once if the first readback is inconsistent;
- publish success after a recovered transient mismatch, while retaining a
  real error only if the second readback still fails;
- log recovered mismatch and persistent failure details for diagnosis.

Compatibility remains C906L R27P, Linux LN27 and IPC protocol v1 / ABI4.
The R27.6.6.21 safe refresh pacing, R27.6.6.22 portal icon, RootFS, airlinkd,
50 MHz requested/46.875 MHz actual SDIO, Kernel, DTB and ramdisk are unchanged.