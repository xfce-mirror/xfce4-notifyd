#!/bin/bash

LOG_FILE="$HOME/.cache/xfce4/notifyd/log"
LOG_ENABLED=$(xfconf-query -c xfce4-notifyd -p /notification-log)
TIMESTAMP=$(date +%F_%X_%N)

# Check if the log is enabled
if [[ "$LOG_ENABLED" == "true" ]]; then
	echo "Log is enabled"
else
	echo "Log is disabled - enabling..."
	xfconf-query -c xfce4-notifyd -p /notification-log -t bool -s true --create
fi

# Send a test notification
notify-send -a 'xfce4-notifyd-test' "Test notification summary" "Test notification sent at $TIMESTAMP"

# Check if the log file exists
if [ -f "$LOG_FILE" ]; then
        echo "Log file found."
else
        echo "No log file present."
	exit 1
fi

# Check if the notification is found in the log
LOG_CONTENT=$(cat $LOG_FILE | grep $TIMESTAMP)
if [[ "$LOG_CONTENT" != "" ]]; then
	echo "Notification successfully logged."
else
	echo "Notification not logged."
	echo "Haystack: $(cat $LOG_FILE)"
	echo "Needle: '$LOG_CONTENT'"
	exit 1
fi
