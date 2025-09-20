# Direct port debugging

Baud rate is 115200

I used the following command to snoop on the serial port:
`socat -x -v /dev/ttyPulsarDome,rawer,b115200,crnl PTY,link=/dev/ttyV1,rawer,crnl`
(I had some issues with crnl processing, so also used this without it - I'm not sure it made any difference, in rawer mode crnl may be ignored.)
`socat -x -v /dev/ttyPulsarDome,rawer,b115200 PTY,link=/dev/ttyV1,rawer`
Once open, change access rights:
`ls -alr /dev/ttyV1` to get the actual pts, followed by e.g. `chmod 0666 /dev/pts/3`

Then point the indi driver at `/dev/ttyV1`!

I'm not sure why, but trying to use `picocom` to talk to the serial port didn't work - as soon as I typed a character (e.g. `P`), a response of `E,1` or similar was given.

Instead, I used the following to transmit a command:
`echo -n -e "PULSAR\r" >/dev/ttyV1`

There's definitely something weird going on with the command processing, and using echo in this way seems to work best.

## Dev fake serial port
`socat -d -d pty,raw,echo=0 pty,raw,echo=0 &`
will output two pts devices. Connect the driver to one of these, e.g. `/dev/pts/1`, and then echo responses to the other end.
E.g. `echo -n -e "Y123\r" >/dev/pts/2`

## Commands and their output formats:

# PULSAR
Y519

# VER
2.23

# BAT
1000[TAB]4165[TAB]0[TAB]17000[TAB]0

# SHUTTER
`0` open
`1` closed
`2` opening
`3` closing
`4` error
`5` unknown
`6` not fitted

# OPEN
Open shutter

# CLOSE
Close shutter

# ENCREV
25053.00

# ANGLE
0.0

# HOME
The home command seems to get the home position. Returns an angle, e.g. `0.0`

# HOME ?
Returns a `1` if the dome is in the home position.

# HOME %3.1f 
Set the home position to the angle provided.

# GO H
Go to the home position.

# PARK
The park command seems to get the park position. Returns an angle, e.g. `0.0`

# PARK ?
Returns a `1` if the dome is in the park position.

# PARK %3.1f 
Set the park position to the angle provided.

# GO P
Go to the park position.

# V
0.0[TAB]0[TAB]0.000000[TAB]0.0[TAB]0[TAB]1[TAB]1000[TAB]4165[TAB]0[TAB]-71[TAB]17000[TAB]0[TAB]0

13 fields
1 = Current AZ position
2 = Motor state, but may not match MSTATE, I've seen `1` when moving to target, `8` when calibrating, `9` when moving to home
3 = 
4 = Target AZ position
5 = Movement direction, `1` clockwise, `2` anti-clockwise
6 = Shutter state
7 =
8 =
9 =
10 =
11 = 
12 =
13 =

# MSTATE
Get the motor state.
`0` idle
`1` moving to target
`2` moving to velocity
`3` moving at sideral
`4` moving ccw
`5` moving cw
`6` calibrating
`7` homing

# BBOND %d
Not sure about this one, seems to connect or disconnect shutter by setting a `1` or `0`.

# BBOND
Not sure about this one, believe it checks if shutter connected.

# BTFORCE

??

# ANGLE K %3.1f

Sync

# GO %3.1f

Presumably go angle

# CALIBRATE

# STOP
