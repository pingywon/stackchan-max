# Things that bit us

The mistakes that cost real time while building this, in short form.

Short versions. The long ones, with file names, are in [docs/STATUS.md](STATUS.md) and
[`workshop/notes/FINDINGS.md`](../workshop/notes/FINDINGS.md).

- **Stock firmware overwrites yours.** At boot it installs whatever M5Stack's server
  offers, without asking. Our build ran once, then the robot was back on stock.
- **The first build was never looked at.** MAX's eyebrows were the same colour as his
  hair, so all six emotions looked identical. One screenshot would have caught it. That
  is why the simulator exists, and why nothing ships unrendered now.
- **A file search for `*.cpp` missed the `.cc` files.** One of them is where the AI screen
  builds its face. The chosen skin was invisible in normal use for three builds while
  looking correct everywhere else.
- **`%llu` crashed the robot.** The small `printf` on this chip does not support 64-bit
  numbers. Printing an uptime shifted every later argument and killed the web server
  within seconds. It looked like the AI was at fault. A script now checks for it.
- **Two wake-word detectors were running at once.** A setting we never touched was
  filling in a second model by itself. It worked with four phrases. With eleven, free
  memory fell to 163 bytes and nothing was heard at all.
- **Tasks that silently never start.** Three separate times, a task was created on the
  small internal memory and nobody checked whether that worked. The fix each time was
  the same: put the stack in the 8 MB of external RAM.
- **Nothing checks the size of the assets area.** An oversized image "built fine" and
  failed at flash time. The build now fails with the overflow in bytes.
- **The settings page is one long C++ string.** A JavaScript mistake in it is invisible
  to the compiler, and a dead script looks like a styling bug. `check_page.py` parses it
  before every build.
- **A settings field that did nothing.** The portal saved a URL under one storage name
  and the firmware read another. It was inert from the day it was added. Always check
  what the reading side actually reads.
- **The SD card and the screen share a wire.** On the CoreS3, GPIO35 is the card's
  data-in line *and* the screen's data/command line. Until that is solved at the driver
  level, the music feature reports "no card" instead of risking the display.
- **A new source folder needs `idf.py reconfigure`.** The file list is cached. A plain
  build compiles cleanly and then fails to link, missing symbols that are plainly there.

Known rough edges today:

- The skin and wake-phrase names on the portal's Face and Wake word pages are hard to
  read (dark text on a dark label). Two styles share one class name.
- The portal's "Restore stock firmware" button is not set up in a default build. It needs
  the address of a stock app image that you host yourself, and M5Stack publish no such
  file. Until one is set, the button only explains what to do. The USB kit does the same
  job without it.
- The fork is three commits behind `m5stack/StackChan`.
