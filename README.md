# Slip

A CDJ's slip mode for any track in Live. It records the track all the time, so it always knows where the track would be. A gesture moves a second playhead away from that: hold the platter, play it backwards, jog the pitch, scratch, or fire a brake or backspin. Pitch and time move together, like vinyl. When you let go, it snaps back to where the track is now. That can be at once, like a CDJ, or on the next beat or bar line. Either way the track never loses its place in the bar.

Put it on the track (audio effect, VST3 or AU).

## Gestures

Each gesture is a setting, so Live's Key Map and MIDI Map can play it.

| | |
|---|---|
| **Hold** | Hand on the platter: it slows to a stop over the Inertia time and waits. A mapped key toggles it: press to grab, press again to let go. |
| **Reverse** | Plays backwards at 1x while on. |
| **Jog** | Pitch nudge, up to ±Jog Range semitones. The slider springs back to the centre. |
| **Scratch** | Platter position for a mapped knob or automation. Moving it scratches. After it has been still for 0.12 s it lets go. |
| **Fire** | Plays the Gesture (Brake, Backspin, Half speed, Rewind) for its Length, then snaps. Every key press fires it. |
| **Snap back** | Instant, Next beat, or Next bar. On the beat or bar settings, the track picks up from where your gesture left it at 1x, then crossfades back on the line. |

In the window:
- Drag the record to scratch.
- Turn the outer ring to jog. It springs back when you let go.

MIDI in (send a MIDI track's MIDI To this track):
- C1 holds while the note is down.
- C#1 reverses while the note is down.
- D1 fires.
- The pitch-bend wheel jogs, and springs back by itself.

## Limits

It can't go ahead of the track: that part hasn't played yet. Jogging faster than 1x catches up with the track and stays there. A hold lasts up to about 40 seconds. The output runs 4 samples late, and Live compensates for that.

## Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
build/SlipTests_artefacts/Release/SlipTests
```

To get a picture of the window, run `SLIP_SNAPSHOT=out.png build/SlipTests_artefacts/Release/SlipTests "Editor*"`.

`source/plugin/Spotlight.h` is the shared Lvrs header that lights up whatever Key Map or MIDI Map is moving.
