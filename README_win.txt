Curvessor 2.1 — Windows

Audio plug-in by Dario Mambro / unevens
https://www.unevens.net
Source code: https://github.com/unevens/Curvessor


WHAT'S IN THIS ZIP

  Curvessor 2.1.vst3        VST3 plug-in
  legal/                    Full license texts for Curvessor and every
                            third-party library it links against
  README.txt                This file


REQUIREMENTS

  Windows 10 or later, 64-bit
  A VST3-compatible DAW (Reaper, Live, Bitwig, FL Studio, Cubase, ...)


INSTALL — STEP 1: UNBLOCK THE ZIP (only if downloaded from the web)

  Windows tags files downloaded from the internet with a "Mark of the
  Web" flag that can cause your DAW to refuse to load Curvessor or
  silently skip it during scanning.

  Before extracting, right-click the downloaded .zip, choose
  Properties, and if you see an "Unblock" checkbox at the bottom of
  the General tab, tick it and click OK. Then extract the zip.

  If SmartScreen warns you when running anything from the extracted
  folder, click "More info" then "Run anyway".


INSTALL — STEP 2: COPY THE PLUG-IN

  Copy the "Curvessor 2.1.vst3" folder (the whole folder, not just
  the file inside it) to ONE of the locations below. Use the
  per-user path if you want to install for just your account, or
  the system path if you want every user on this PC to see Curvessor.

  Per-user (no admin needed):
    %LOCALAPPDATA%\Programs\Common\VST3\Curvessor 2.1.vst3

  System-wide (admin required):
    C:\Program Files\Common Files\VST3\Curvessor 2.1.vst3

  Tip: paste %LOCALAPPDATA%\Programs\Common\VST3 into File Explorer's
  address bar — Windows will expand the path to your user folder. If
  the VST3 folder doesn't exist yet, create it.


INSTALL — STEP 3: RESCAN IN YOUR DAW

  Restart your DAW. Some hosts need an explicit "Rescan plug-ins"
  before Curvessor shows up in the plug-in menu.


WHY NO CODE SIGNATURE?

  Curvessor is free, open-source software. Distributing it with a
  Microsoft-trusted code-signing certificate would require paying
  for one annually, which doesn't make sense for a free GPL plug-in.
  The Unblock step above just tells Windows that you trust this
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
