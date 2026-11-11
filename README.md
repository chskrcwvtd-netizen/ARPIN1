# PhraseArp

PhraseArp is a MIDI arpeggiator instrument. It reads a held chord or an internal chord track, keeps the line in key, and closes the phrase. The same seed, input, and musical position produce the same MIDI live and offline.

The Init preset is a finished 4-bar line in C major over C, Am, F, C. Press Generate for a new line from the seed. Drag MIDI or Export writes the phrase you hear, from the start of the phrase, as a Standard MIDI File (960 PPQ).

## Build

Requirements: CMake 3.22 or newer, a C++20 compiler, and the JUCE 8 Linux dependencies (ALSA, FreeType, fontconfig, X11, OpenGL). JUCE itself is fetched at configure time (tag `8.0.17-security`) and is not vendored. JUCE is GPL unless you have your own commercial JUCE license; this repository does not grant one.

```bash
sudo apt-get install build-essential cmake pkg-config \
  libasound2-dev libfreetype6-dev libfontconfig1-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev \
  libgl1-mesa-dev libglu1-mesa-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

If JUCE is already on disk, point CMake at it instead of cloning:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
```

Engine tests do not need JUCE:

```bash
cmake -S . -B build-engine -DPHRASEARP_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-engine
./build-engine/phrasearp_tests
```

With the plugin build, the same test binary is `./build/phrasearp_tests`.

Outputs:

- Standalone: `build/PhraseArp_artefacts/Release/Standalone/PhraseArp`
- VST3: `build/PhraseArp_artefacts/Release/VST3/PhraseArp.vst3`

This tree was configured and built on Linux (Standalone and VST3). No Windows or macOS binary was produced here. The sources are platform-independent C++20 and CMake; the VST3 target is part of the JUCE CMake setup on Windows and macOS as well.

## Tests

```bash
./build/phrasearp_tests
```

The engine tests cover chord recognition, voice leading across a chord change, strong-beat chord tones, phrase closure, question-and-answer, identical output for the same seed, chunk-size invariance, probability 0/100/50, lane lock, state and MIDI round-trips, rejected imports, the preview voice, and the lock-free swap.

## Play the Init phrase

Open the Standalone, or load the VST3 as an instrument. Audition is on by default: while the host transport is stopped and the host still calls the plugin, the chord track plays on an internal clock at the host tempo, so Init is already a closed phrase. Held notes replace the chord track. Preview plays the generated line through the stereo output. It does not play the held chord, and it ignores CC.

If a host suspends processing while the transport is stopped, start playback or play a note. The Standalone keeps the audio device open.

## FL Studio 25 MIDI output

Host MIDI output was **not** verified. FL Studio 25 was not available when this plugin was built.

1. Load PhraseArp on an instrument channel. It is a VST3 instrument (`IS_SYNTH`), not a MIDI effect.
2. Open the wrapper settings for that plugin.
3. Set PhraseArp's MIDI Output port to a free number, for example 1.
4. On the target instrument, set MIDI Input port to that same number.
5. Leave Thru off unless the held chord itself should also reach the target. CC64 is the latch pedal and is not forwarded.
6. Press play, or use Audition while the host is still processing.
7. Drag the **Drag MIDI** handle into the piano roll or the playlist. **Export** writes the same file. **Capture** writes the last 64 bars that were actually sent.

The same steps are in the plugin's Setup tab. If the target stays silent, use Drag MIDI, Export, or the built-in preview. Those do not depend on host MIDI routing.

## What was not run

- FL Studio 25, and any other host's MIDI-output routing.
- Windows and macOS builds.
