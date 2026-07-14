
## GBADoom SVN

A slightly modified version of **[doomhack](https://github.com/doomhack/GBADoom)**'s amazing port of prBoom to the GBA.  
A pre-compiled shareware rom can be found over at the **[Releases](https://github.com/Kippykip/GBADoom/releases)** tab, while a developer branch can be found as **[in-dev](https://github.com/Kippykip/GBADoom/tree/in-dev)**

It basically adds depth fog and a ton of optimizations to the exsisting fork. Enable fog in options.

Be sure to check [doomhack's main branch](https://github.com/doomhack/GBADoom) for future engine optimisations and bug fixes!

## Cheats:
**Chainsaw:** L, UP, UP, LEFT, L, SELECT, SELECT, UP  
**God mode:** UP, UP, DOWN, DOWN, LEFT, LEFT, RIGHT, RIGHT  
**Ammo & Keys:** L, LEFT, R, RIGHT, SELECT,UP, SELECT, UP  
**Ammo:** R, R, SELECT,R, SELECT,UP, UP, LEFT  
**No Clipping:** UP, DOWN, LEFT, RIGHT, UP, DOWN, LEFT, RIGHT  
**Invincibility:** A, B, L, R, L, R, SELECT, SELECT  
**Berserk:** B, B, R, UP, A, A, R, B  
**Invisibility:** A, A, SELECT,B, A, SELECT, L, B  
**Auto-map:** L, SELECT,R, B, A, R, L, UP  
**Lite-Amp Goggles:** DOWN,LEFT, R, LEFT, R, L, L, SELECT  
**Exit Level:** LEFT,R, LEFT, L, B, LEFT, RIGHT, A  
**Enemy Rockets (Goldeneye):** A, B, L, R, R, L, B, A  
**FPS Ammo Counter:** A, B, L, UP, DOWN, B, LEFT, LEFT  

## Controls:  
**Fire:** A 
**Use / Sprint:** b  
**Walk:** D-Pad  
**Strafe:** L & R  
**Automap:** SELECT  
**Weapon up:** l + R+ up 
**Weapon down:** L + R+ down
**Menu:** Start  

## Building:
folow kippys tutorial then lastly run make IWRAM_MIN_GAP=2048 in your terminal. 

7) Copy GBADoom.gba (this is the rom file) to your flash cart or run in a emulator.
