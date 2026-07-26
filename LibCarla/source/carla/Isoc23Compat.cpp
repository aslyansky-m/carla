// Copyright (c) 2026 Computer Vision Center (CVC) at the
// Universitat Autonoma de Barcelona (UAB).
//
// Ubuntu 24.04's glibc headers redirect strtol to the C23 entry point while
// CARLA's pinned Unreal Linux SDK intentionally targets an older glibc. Keep
// host-built third-party archives compatible with that SDK by forwarding the
// new spelling to the long-standing ABI symbol.

#if defined(__linux__)

extern "C" long int LegacyStrtol(
    const char *String,
    char **End,
    int Base) asm("strtol");

extern "C" long int __isoc23_strtol(
    const char *String,
    char **End,
    int Base)
{
  return LegacyStrtol(String, End, Base);
}

#endif
