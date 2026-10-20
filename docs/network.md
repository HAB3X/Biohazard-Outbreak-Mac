# Network Play on obsrv.org

Both discs' Network Play runs on the Mac against obsrv.org's revived servers: the network setup, DNAS and the KDDI browser up
to obsrv's login page, with the Mac's keyboard typing into the game's software keyboard. The owner logged in (File 2,
English) and the lobby ran for five minutes; joining a game stopped at the game server (a connection's option the wrapper
didn't have), now done. Rooms and games past that are untried.

The runtime's side (AVE-TCP on the Mac's sockets, the network configuration, DNAS's IOP helper and its overlays decrypted
ahead of time, the USB keyboard) is in its `docs/network.md`; this is the games' side. Runtime branch `biohazard-online`,
this repository's `online`.

## How to play online

1. Start the game (`./run.sh file2` or `./run.sh file1`), at the main menu choose NETWORK PLAY (ネットワークプレイ), then
   confirm the caution screen.
2. The first time, the game asks to make its NET file on the memory card (はい / yes) and, on a blank card, to format it
   (はい): that's the game's own data, not a network setting.
3. Broadband connection (ブロードバンド接続): choose Network connection (ネットワーク接続), then 進む (proceed) on the manners
   screen.
4. The game saves the connection to the NET file and connects (DHCP, DNAS: a progress bar to 100%); the connection list
   isn't shown (below). The browser opens obsrv's page: "Login with forum account: ID / Password / LOGIN", and "Auto login
   through forum" when the forum knows this network's address.
5. Logging in, with your obsrv.org forum account (sign up on obsrv.org's website first):
   - The pointer starts on the ID field. Press E (circle) to open the software keyboard. Wait until it's drawn, then type
     your ID on the Mac's keyboard (Shift for capitals; Backspace deletes; the arrows move over the soft keys instead).
     Press Return to put it in the field.
   - Press Down (S or the down arrow) to move to the Password field, E, type the password, Return. The field shows a row of
     asterisks whatever the length.
   - Move down to the LOGIN button under the fields and press E. (Or, when the page says "Signed in as ...", its own LOGIN
     button logs in through the forum without typing.)

Typing goes to the game only while the software keyboard is up; the rest of the time the keys are the pad's as usual. When
the Mac is busy (a few frames a second), wait for the keyboard to be drawn before typing and type at a moderate pace: the
game reads a keyboard of three reports at a time, as on a console, and drops keys typed faster than it reads them.

## What the game does, and what stands in

| Step | The game | On the Mac |
|---|---|---|
| Modules | loads its network modules through Capcom's LOADHIGH.IRX (the USB searcher, the AVE-TCP wrapper, AVE-TCP, its device glue, DHCP, DEV9/SMAP) | `src/loadhigh.cpp` loads each through the runtime's loader; `src/usbsrch.cpp` answers the USB searcher (no USB modem or adaptor); `src/avewrap.cpp` stands in for Capcom's wrapper (RPC 0x01270030) on the runtime's AVE-TCP |
| Configuration | looks for "Your Network Configuration" on the card, then asks netcnf for its entries | `netwk_card_has_netcnf` (File 2 0x5C84F0, File 1 0x5D0C00) answers "there"; the runtime's netcnf presents the Mac's network, DHCP, DNS 208.72.237.31 (obsrv's, `src/network.h`) |
| Connection list | the setup screen (netwk's step, File 2 0x5BFD40, File 1 0x5C88C0) lists "edit the network settings", "proxy settings" and the card's configurations | the list is skipped: when it would come up, `netwk_setup_step` (`src/network.cpp`) puts the game where choosing the Mac's configuration puts it (the list's state 0x12: connect, save it as the last connection). If that connection fails, the list's own "stop connecting?" dialog comes up instead (No tries again). The settings rows started Sony's setup program from the disc with LoadExecPS2 (NTGUI2/YN_FILEJ.BIN, after shutting the game's pads and sound down: 0x5C5340); the runtime can't run another program (syscall 6 isn't done), so the game went on with its pads closed: the freeze seen there |
| Connection | DHCP on the adaptor, the link checked | the Mac's address, mask and router |
| DNAS | HTTPS to gate1.jp.dnas.playstation.org (DNAS 2.8, its overlays netdnas and insdnas decrypting themselves) | obsrv's DNS names its DNAS (153.121.44.150); the overlays are decrypted at build time (`decrypt` in config/file*.toml); the runtime answers the DNAS IOP helper |
| Browser | the KDDI browser (nethttp overlay) over HTTPS to www01.kddi-mmbb.jp | obsrv's server (208.72.237.15); its page is /00000010/ for File 2, /00000002/ for File 1 |
| Typing | the software keyboard (netwk's step, File 2 0x552F10, File 1 0x553820, once a frame while it's up) reads the USB keyboard through libusbkb | the step's override (`src/typing.cpp`) tells the runtime the game takes typed text, so the Mac's keys reach its USB keyboard |
| Lobby, games | the lobby (TCP to obsrv's 8200), then the game server (TCP to 8590, where it gets and sets option 0x2008, no Nagle) | the wrapper's functions 3 and 4 and AVE-TCP's options; the lobby worked in the owner's run, the game server untried since |

Functions the recompiler didn't find on its own (only reached through pointers made at run time) are in the configs'
`extra` lists: netwk's 0x566120 (File 2) and 0x565FE0 (File 1), from DNAS's progress screen. nethttp's `extra` lists had
switch-case labels from `tools/pointer_functions.py`, which made bogus function starts in the middle of functions; the tool
now leaves them out.

## Checked (2026-10-20)

- File 2 and File 1: title to obsrv's login page, DNAS 100% each time (log: `AVE-TCP: connected to 153.121.44.150:443`,
  then `208.72.237.15:443`; pictures of the login page on both discs).
- Typing: on both discs the software keyboard took "Test1" from the Mac's keyboard (Shift for the capital, Backspace),
  Return put it in the ID field; on File 2 "test" in the Password field too. A memory dump showed the browser's form
  holding username "Test1" and password "test". Nothing was submitted: no connection after the page's own.
- Single play unchanged: File 2's Training Ground loads and Kevin walks (the keys are the pad's outside the software
  keyboard).

## What the game asks of the network

Every call of the EE's network library goes through one function to the wrapper's RPC server (File 2's 0x1C75B0, File 1's
0x1C3CD0). Their callers ask for functions 1-7, 11, 13, 15, 16, 19, 22, 30, 31, 33-49, 54-56, 58-63, 70-72 and 74 (both
discs), all in `src/avewrap.cpp` but the modem's PPP (33-40). Not one asks for a passive open (8-10) or UDP (81-84): the
game's online play is TCP to obsrv's servers, which relay games (obsrv-pack's servers listen on TCP only). Every receive
(function 19) follows a status call that said data is waiting (File 2's 0x1CABC0, and 0x1D01E0 through 0x1D0490), so it
never waits. The wrapper's code is the same on both discs (four bytes of data differ), and so is AVE-TCP's.

## Not done

- After the game server: rooms, games with others, handle name and chat (the handle name and chat use the same software
  keyboard, so typing should work there, untried). The owner's next run shows how far it gets.
- The mouse in the browser (it has its own pointer, moved by the pad).
- PPP (modems), the HDD install (insdnas's own DNAS), the network settings screens (they'd write files nothing reads).
