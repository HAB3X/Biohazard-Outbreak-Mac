# 3. The PC menus in Outbreak's own look

## Context

The PC menus (F10) are the shared runtime's, drawn in the Crash Twinsanity port's look: a blue spiral, cartoon fonts, a white
panel with orange. Over Biohazard Outbreak they looked out of place.

## Decision

The runtime's menus take their whole look from a theme the game names (`Ps2GameInfo::menuTheme`, the runtime's
`ui/menutheme.h`); the layout and behaviour stay the runtime's. "crash" is the old look, unchanged (the same pictures, but for
the moment of the animated spiral). "outbreak" follows the game's own pause and status screens: the game's picture dimmed
behind, dark navy panels in a lavender-blue frame, the tabs as the status screen's ITEM / SPECIAL / FILE (the chosen one
orange-brown), the chosen row green as the pause menu's EXIT, condensed capitals with a blue outline as its PAUSE. Its fonts
are the Mac's own DIN Condensed and DIN Alternate (read at run time, not shipped; the bundled fonts if they're missing).

## Rejected

- The game's own pictures and font taken from the disc at first launch: the truest look, but the menus' frames are drawn by
  the game from many small textures and its font is a 20 x 20 grid of full-width glyphs; reproducing its colours and shapes
  gets the look for a fraction of the work. Still possible later behind the same theme.
- A copy of the menus' code per game: the look is all that differs, so it's the only thing that varies.
