GBADoom V3 optimization port with half-resolution floors
=========================================================

Purpose
-------
This applies doomhack/GBADoom branch V3 to:
Cavencruiser/GBADoom-kippy-retail-gba-controls-merge-

It then reverts only upstream commit:
e5d5c5b6c2351c7de05a5a3ac9ab4a794f1a9ec4
"Full resolution floor textures."

That commit is the one that changes the span renderer from the old
half-resolution floor/ceiling path to paired full-resolution pixels.
Reverting it keeps the target repo's cheaper half-resolution floors while
retaining the rest of the V3 optimization series.

How to use
----------
1. Make a backup of your current project.
2. Make sure Git is installed and the project has no uncommitted changes.
3. Copy apply_v3_halfres.ps1 and apply_v3_halfres.bat into the repository root.
4. Double-click apply_v3_halfres.bat, or run:

   powershell -ExecutionPolicy Bypass -File .\apply_v3_halfres.ps1

5. The result is placed on a new branch named:

   v3-optimized-halfres

6. Build normally with make.

Notes
-----
- This method preserves commit ordering and dependencies. Copying isolated final
  V3 renderer files over the kippy fork would overwrite fork-specific controls,
  HUD and build changes and is substantially more likely to break.
- Git may report conflicts because the target is a customized fork. The script
  stops without discarding anything and prints the continuation command.
- The full-resolution commit also contains unrelated temporary IWAD/IRQ edits;
  reverting it removes those edits as well, which is intentional and does not
  remove later optimization commits.
