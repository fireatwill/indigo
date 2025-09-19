# Direct port debugging

Baud rate is 115200

I used the following command to snoop on the serial port:
`socat -x -v /dev/ttyPulsarDome,rawer,b115200,crnl PTY,link=/dev/ttyV1,rawer,crnl`
Once open, change access rights:
`ls -alr /dev/ttyV1` to get the actual pts, followed by e.g. `chmod 0666 /dev/pts/3`

Then point the indi driver at `/dev/ttyV1`!

I'm not sure why, but trying to use `picocom` to talk to the serial port didn't work - as soon as I typed a character (e.g. `P`), a response of `E,1` or similar was given.

Instead, I used the following to transmit a command:
`echo -n -e "PULSAR\r" >/dev/ttyV1`

There's definitely something weird going on with the command processing, and using echo in this way seems to work best.

## Commands and their output formats:

# PULSAR
Y519

# VER
2.23

# BAT
1000[TAB]4165[TAB]0[TAB]17000[TAB]0

# SHUTTER
1

I belive 0 = Open, 1 = Closed, 2 = Opening, 3 = Closing

# ENCREV
25053.00

# ANGLE
0.0

# HOME
0.0

# HOME ?

Is dome at home?

# V
0.0[TAB]0[TAB]0.000000[TAB]0.0[TAB]0[TAB]1[TAB]1000[TAB]4165[TAB]0[TAB]-71[TAB]17000[TAB]0[TAB]0

13 fields
1 = Current AZ position
2 = Motor state
3 = ?
4 = ?
5 = Shutter state
6 =
7 =
8 =
9 =
10 =
11 = 
12 =
13 =

# PARK

# MSTATE

Is dome moving?
Values 0 or 3 mean no, not moving. Not sure about that. Motor state, I believe.

# BBOND 1

Connect to shutter

# BBOND

Is connected to shutter?

# BTFORCE

??

# ANGLE K %3.1f

Sync

# GO P

Park

# GO %3.1f

Presumably go angle

# OPEN

Open shutter

# CLOSE

Close shutter

# GO H

Go home

# CALIBRATE

# STOP

# HOME %3.1f 

Sets home location

# PARK %3.1f 

Set park location


