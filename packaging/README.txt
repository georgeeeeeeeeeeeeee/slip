SLIP @VERSION@
==========

CDJ slip mode for any track. Slip records the track all the time, so it always knows where the
track would be. A gesture (hold the platter, reverse, jog, scratch, or a fired brake, backspin,
half speed or rewind) moves a second playhead away from that, pitch and time together like vinyl.
Let go and it snaps back to where the track is now: at once, or on the next beat or bar line.

Formats: AU and VST3 for macOS (Apple silicon and Intel, macOS 11 or later), VST3 for Windows
(64-bit). Free and open source (AGPLv3). Source: @SOURCE_URL@

These builds aren't signed with a developer certificate, so each system needs one extra step.


INSTALL: MAC
------------
1. Copy Slip.component to   ~/Library/Audio/Plug-Ins/Components/
   Copy Slip.vst3      to   ~/Library/Audio/Plug-Ins/VST3/
   (In Finder: Go > Go to Folder..., then paste the path.)
2. Open Terminal and clear the download quarantine:
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Slip.component
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Slip.vst3
3. In your DAW, rescan plug-ins. In Live: Settings > Plug-Ins, turn on the AU and VST3 system
   folders, then Rescan.

If macOS says the plug-in "is damaged" or "can't be opened", step 2 was missed.


INSTALL: WINDOWS
----------------
1. Copy the Slip.vst3 folder to   C:\Program Files\Common Files\VST3\
2. In your DAW, rescan plug-ins. In Live: Options > Preferences > Plug-Ins, turn on the VST3 system
   folder, then Rescan.

If Windows SmartScreen warns about the file, choose "More info", then "Run anyway".


PLAYING IT
----------
Put Slip on the track. Every gesture is a parameter, so Live's Key Map and MIDI Map play it:

  Hold       hand on the platter: it slows to a stop and waits (a mapped key: press to grab,
             press again to let go)
  Reverse    plays backwards
  Jog        pitch nudge; springs back to the centre
  Scratch    platter position for a knob; moving it scratches
  Fire       plays the Gesture (Brake, Backspin, Half speed, Rewind) for its Length, then snaps
  Snap back  Instant, Next beat or Next bar

In the window, drag the record to scratch and turn the outer ring to jog.
MIDI in (a MIDI track's MIDI To this track): C1 holds, C#1 reverses, D1 fires, the pitch wheel jogs.

Slip can't jump ahead of the track: that part hasn't played yet.
