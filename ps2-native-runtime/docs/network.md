# Online play

A PS2 game online runs its own TCP/IP stack on the IOP (ACCESS's AVE-TCP, or SN Systems' stack, or Sony's INET) over the
network adaptor's driver (DEV9 and SMAP), reads its network setup from the memory card ("Your Network Configuration"),
passes Sony's DNAS check (a signed exchange with Sony's servers, now with revived servers' own), and then speaks its own
protocol to its servers. The runtime has no IOP CPU (DESIGN.md, the IOP), so it stands in for each IOP module natively, at
the level of its RPC interface, as it does for sound and pads. What the game's EE side does (its HTTP, its SSL, its DNAS
library, its lobby protocol) runs recompiled and unchanged.

Biohazard Outbreak (File 1 and File 2, Japan) runs it all: Network Play reaches the revived servers' (obsrv.org) login page
on the Mac. The game's side of the work (Capcom's own IOP modules: the AVE-TCP wrapper, LOADHIGH, the USB searcher) is in its
repository's `src/` and docs.

## How the pieces fit

| Console | Mac |
|---|---|
| netcnf.irx + netcnfif.irx: the card's BWNETCNF files | `iop/netcnf.cpp`: one configuration, the Mac's network |
| AVETCP.IRX (TCP/IP, DNS), DEVGLUE.IRX, AVEDHCP.IRX | `iop/avetcp.cpp`, `avetcp_socket.cpp`, `avetcp_dns.cpp`: the Mac's sockets, its address and router, the game's DNS servers asked from the Mac |
| SMAP.IRX, DEV9.IRX (the adaptor) | nothing runs: the Mac's network is the "device", its link always up |
| DNAS's IOP helper, Sys1_driver04 (built in EE memory, loaded from a buffer) | `iop/dnas.cpp`: the console's and disc's identities |
| usbkb.irx (a USB keyboard) | `iop/usbkb.cpp`: the Mac's keyboard while the game takes typed text |
| the DNAS overlays' self-decryption | decrypted ahead of time by the recompiler (`recomp/selfdecrypt.py`) |

### The network: host sockets, not a virtual adaptor

The game's stack could have been kept: emulate the network adaptor (SMAP's registers, DEV9's DMA) and send its Ethernet
frames through a tap device or a user-mode NAT (as PCSX2 does). That needs the IOP's modules to run, which needs an IOP CPU
the runtime doesn't have, and a virtual Ethernet on the Mac needs admin rights (vmnet) or a kernel extension (tap). So the stack's
API is answered instead (AVE-TCP's `AT_apiCall` codes 0x41xx, which every other module and the EE's wrapper call): a TCP
connection is a non-blocking socket of the Mac's, a DNS lookup a UDP query the Mac sends to the servers the game was given.
The game sees the console's results (its handles, BSD's TCP states, AVE-TCP's error numbers), the remote servers see the
Mac's connections, and the Mac's own NAT, firewall and routes apply.

What the stack would set up on its device is answered with the Mac's own: the device list has one device, Sony's network
adaptor (SMAP's "Ethernet" device id 2 on bus 1) once the game loads that driver; its link is up; DHCP "leases" the Mac's
own address, mask and router for good; the routes and addresses the game sets are recorded and not applied.

Addresses and ports in AVE-TCP's blocks are kept in network order (port 443 read as a host number is 47873).

Done: TCP as a client (open, send, receive, status, close, abort; a receive with nothing there waits for data as the
module's does), a connection's options (0x4108/0x4109: keep-alive, no Nagle, and immediate ACKs, which the Mac can't set
per connection: kept for the game to read back), DNS A lookups, DHCP, the device glue's parameters the game reads. Not done
(each stops the game naming what was asked, so a game that needs it shows where): UDP, listening (passive open and
accept), PPP, a receive or send with an ASR (a completion callback). Biohazard Outbreak's EE never asks its wrapper for UDP
or listening (the game's repository lists the wrapper functions both discs call): its online play is TCP to the servers.

A receive that waits holds the whole game: the runtime serves an RPC on the caller's thread, so the EE's other threads
don't run meanwhile as they would on a console. Biohazard Outbreak only receives what a status call said is waiting.

`ps2_avetcp_test` (runtime/tests/avetcp, built on request: `ninja ps2_avetcp_test`) runs the stand-in against a local
server with blocks laid out as a game's wrapper builds them: open and status, the options and their errors, send and
receive, a receive that waits, the peer's FIN before (-10) and during (-8) a receive, an abort.

### The network configuration: a preset, not the game's setup screens

On a console a network setup tool writes the configuration to the card, and the game offers the card's entries. On the Mac
the adaptor and address are the Mac's, so there's nothing to set up: `netcnf.cpp` presents one entry, the Mac's network
(Sony's adaptor, DHCP, and the DNS server the runtime is given), whatever the card holds. The game's own entry editor would
write files nothing reads; adding, editing and deleting entries fail loudly. The DNS server is `--dns ADDRESS`, or the game's
`Ps2GameInfo::networkDns` (a revival's DNS, which names its own DNAS and game servers); with none, the Mac's own resolver
answers. A game that checks the card for the configuration's files before asking netcnf (Biohazard Outbreak does) needs an
override of that check in its own repository.

### DNAS

DNAS 2.8's EE library builds an IOP module in memory ("Sys1_driver04") and asks it, over RPC 0x80000F03, for what only the
IOP can read: the clock, the console's model ID and GUID (i.LINK ID), the disc's ID and the HDD's identity (`dnas.cpp`'s
header has the request and answer formats: a key and a check in each request; each answer a nonce, the payload and a SHA-1
of both, XORed with a stream from a SHA-1 of key and nonce). The Mac answers as a console whose mechacon has no GUID would
(made from the time under Sony's OUI 08:00:46, model 0x1A0002), with no HDD, and with the disc's ID made from its serial
number as PCSX2 models the mechacon's key (the ID is on the disc's lead-in, not in its image; without one DNAS fails with
-402). The revived servers' DNAS then answers the game's HTTPS exchange (the SSL is the game's own EE code).

DNAS's overlays decrypt parts of themselves as they run. The recompiler decrypts them ahead of time
(`recomp/selfdecrypt.py`): each decryption routine an overlay's config names (`decrypt = ["0x...", ...]`) is found at its
call sites with its arguments (the call's inline descriptor: the block's size as `a1 ^ a2`, whether it's encrypted again
after, the block), and run in a small R5900 integer interpreter over the overlay's image, so the plain code is what's
recompiled. An overlay with `decrypt` is `encrypted`: its check words (DESIGN.md, overlays) are only words the decryption
leaves the same, so the loaded (still encrypted) image and the decrypted one are both recognised, and functions inside a
decrypted block take their first words from the plain image.

### Typing: the game's USB keyboard

Network play takes text (a login, a handle name, chat) through a software keyboard drawn by the game, which also reads a USB
keyboard through libusbkb. `usbkb.cpp` stands in for Sony's usbkb.irx (RPC 0x80000211: a keyboard on port 1, a ring of three
HID boot reports, its LED and lock-key handling); `input.cpp` feeds it the Mac keys held as a boot report while the game
says it's taking text (`Ps2GameInfo::typing`), so those keys type instead of pressing the pad's buttons. The arrows stay the
pad's d-pad (they move over the software keyboard) and the PC menus' key stays theirs; a key already held when typing starts
is left out until it's let go. libusbkb reads a report whose modifiers changed as that change alone, so a report whose
modifiers and keys both changed is sent as two (Shift and a letter pressed within one input update would lose the letter).
The ring holds three reports, as the module's does: at a few frames a second, keys typed faster than the game reads them
drop as they would on a console.

## Debugging

`$PS2_DUMP_MODULES=DIR` writes each IOP module a game loads from a buffer (DNAS's helper, Capcom's) to DIR, to read it.
The network stand-ins log each connection, DNS answer and DHCP lease (`AVE-TCP:` lines).
