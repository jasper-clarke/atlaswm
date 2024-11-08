#!/usr/bin/env bash

# Start Xephyr
Xephyr :100 -ac -screen 1280x720 &

# Wait a moment for Xephyr to start
sleep 1

# Set the DISPLAY to Xephyr
export DISPLAY=:100

# Start your window manager
./result/bin/atlaswm &

# Start a terminal (assuming you have xterm installed)
xterm &
xterm &
