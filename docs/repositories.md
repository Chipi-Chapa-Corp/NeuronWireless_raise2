# Repository structure

Only repositories containing modified source are forked. Unmodified dependencies
remain at their original Dygma commits. The custom branches start from the source
pins of `Raise2_v1.5.0`, rather than current upstream development branches.

```text
Chipi-Chapa-Corp/
├── NeuronWireless_raise2       branch: raise2-v1.5.0-splash
│   ├── src/SplashHeatmap*      animation, LED ownership and Focus on/off
│   ├── tools/                 host helper and patcher compatibility entry point
│   ├── tests/splash/           host tests and side instruction emulation
│   ├── build/                 build/package scripts; generated files ignored
│   ├── libraries/             submodule → NeuronWirelessLibs
│   └── side-patches/          submodule → raise2-side-patches
├── NeuronWirelessLibs          branch: raise2-v1.5.0-splash
│   ├── Spi_slave/SpiPort.cpp   propagate FIFO enqueue failure
│   ├── Communications/        submodule → DefyCommunications
│   ├── Kaleidoscope/           submodule → Kaleidoscope-Dygma
│   └── other dependencies     original upstream commits
├── DefyCommunications         branch: raise2-v1.5.0-splash
│   ├── notification handling  reject false wired disconnects while link is live
│   ├── Neuron packet filter   prevent competing LED packets entering queues
│   └── side map handlers      source correction matching the blob patch
├── Kaleidoscope-Dygma         branch: raise2-v1.5.0-splash
│   └── KeyboardManager.cpp    BLE disconnect only clears matching BLE cache slot
└── raise2-side-patches         branch: main
    ├── tools/patch-side-led-maps.py
    ├── patches/raise2-v1.5.0.json
    └── tests/test_patch.py
```

Every submodule is pinned to a commit. Branch names describe the development
lines; normal submodule update checks out the recorded commit, not the latest
branch head. Do not use `git submodule update --remote` when reproducing a build.
`tests/splash/upstream-pins.json` records all original 1.5.0 dependency revisions.
Checks require changed libraries to descend from those pins and unchanged
libraries to retain their exact commits. The package manifest records the source
commit and recursive submodule revisions.

## Clone

Some unchanged upstream nested submodules use SSH URLs. The command-local URL
rewrite below permits public HTTPS cloning without changing those upstream repos
or the user's Git configuration:

```bash
git -c url.https://github.com/.insteadOf=git@github.com: clone   --branch raise2-v1.5.0-splash --recurse-submodules   https://github.com/Chipi-Chapa-Corp/NeuronWireless_raise2.git
cd NeuronWireless_raise2
```

For an existing checkout:

```bash
git -c url.https://github.com/.insteadOf=git@github.com:   submodule update --init --recursive
```

The SDK and compiler are external build prerequisites; see the main README and
[Splash build instructions](splash-heatmap.md). Firmware images, local rollback
archives, logs, Python caches and SDK files are not added by this fork.

## Work across repositories

Commit Communications and Kaleidoscope changes first, then commit their new
gitlinks in the library fork, then commit the library gitlink in the main firmware fork. Patch changes
are committed in `raise2-side-patches` and pinned directly by the main repository.
This keeps changes attributable to their upstream source repositories and allows
small upstream PRs later without mixing animation code into dependency fixes.

The standalone patch repository stores the patch's intent, exact input/output
hashes, original/replacement bytes and CRC rules. It downloads no firmware on its
own; the main build downloads the official release if the local input is absent.
Generated patched binaries belong in build artifacts or releases, not Git history.
