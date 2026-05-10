Curvessor 2.1 — macOS

Audio plug-in by Dario Mambro / unevens
https://www.unevens.net
Source code: https://github.com/unevens/Curvessor


WHAT'S IN THIS ZIP

  Curvessor 2.1.component   Audio Unit (AU) plug-in
  Curvessor 2.1.vst3        VST3 plug-in
  legal/                    Full license texts for Curvessor and every
                            third-party library it links against
  README.txt                This file


REQUIREMENTS

  macOS 10.15 (Catalina) or later
  Apple Silicon or Intel
  An AU- or VST3-compatible DAW (Logic Pro, Reaper, Live, Bitwig, ...)


INSTALL — STEP 1: COPY THE PLUG-INS

  Copy each plug-in to ONE of the locations below. Use the per-user
  paths if you want to install for just your user account, or the
  system paths if you want every user on this Mac to see Curvessor.

  Per-user (no admin password needed):
    ~/Library/Audio/Plug-Ins/Components/Curvessor 2.1.component
    ~/Library/Audio/Plug-Ins/VST3/Curvessor 2.1.vst3

  System-wide (admin password required):
    /Library/Audio/Plug-Ins/Components/Curvessor 2.1.component
    /Library/Audio/Plug-Ins/VST3/Curvessor 2.1.vst3

  Tip: in Finder, hold Option and click the "Go" menu to reveal the
  hidden ~/Library folder.


INSTALL — STEP 2: LIFT THE QUARANTINE FLAG

  macOS quarantines anything downloaded from the internet that
  isn't signed with a paid Apple Developer ID. Without this step,
  your DAW will refuse to load Curvessor or silently skip it during
  scanning.

  Open Terminal (Applications → Utilities → Terminal) and run the
  two commands matching the paths you used in step 1. For per-user
  install:

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Curvessor\ 2.1.component
    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Curvessor\ 2.1.vst3

  For system-wide install (you'll be prompted for your password):

    sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/Components/Curvessor\ 2.1.component
    sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/Curvessor\ 2.1.vst3


INSTALL — STEP 3: RESCAN IN YOUR DAW

  Restart your DAW. Some hosts (Logic, Live) need an explicit
  "Rescan plug-ins" before Curvessor shows up in the plug-in menu.


WHY THE QUARANTINE STEP?

  Curvessor is free, open-source software. Distributing it with an
  Apple-trusted signature would require paying Apple $99/year for a
  Developer ID, which doesn't make sense for a free GPL plug-in.
  The xattr command above just tells macOS that you trust this
  download — the plug-in itself is unchanged and runs identically
  to a signed build.


LICENSE

  Curvessor is released under the GNU General Public License v3.0.
  See legal/Curvessor.txt for the per-file copyright header and
  legal/GPL3-LICENSE.txt for the full license text. Third-party
  libraries are listed in their own files inside the legal/ folder.


SUPPORT

  Bugs and feature requests:
    https://github.com/unevens/Curvessor/issues

  Contact:
    hi@unevens.net
