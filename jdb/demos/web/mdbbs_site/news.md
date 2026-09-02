{green}========================================{/}
{yellow} N E W S{/}
{green}========================================{/}

{cyan} ---- WiFi on the Fruit Jam ----------{/}

 The radio turned out not to be what
 anyone assumed. It is an {white}ESP32-C6{/}
 carrying nina firmware, which runs
 its own network stack, so the board
 sends commands instead of packets.

 The verbs are the ones every other
 board already had, so a program that
 fetches a page does not care which
 radio is underneath it. Encrypted
 fetches work too: the certificates
 live on the radio.

{cyan} ---- Programs that skip the compiler -{/}

 Sixteen kilobytes of source took the
 board the better part of a minute to
 translate. The same program built on
 a desktop and copied across starts
 {green}at once{/}.

 Builtins are called by slot number,
 and the numbers differ between
 builds, so a compiled file now
 carries the names and the loader
 rewrites them. A builtin the board
 does not have is refused by name
 rather than {red}called by accident{/}.

{cyan} ---- Controllers ---------------------{/}

 A USB pad is a joystick under the
 same names a desktop uses, so a game
 written against a controller on a PC
 runs on the board unchanged.

{cyan} ---- Keys that repeat ----------------{/}

 Held keys repeat now, and Page Up and
 Page Down move by a screen. Games ask
 the other way round: whether a key is
 down this instant, so a ship steers
 while the key is held.

 {orange}[Main menu](index.md){/}
{green}========================================{/}
