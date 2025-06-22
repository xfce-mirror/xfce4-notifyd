#!/bin/bash

# Script to (manually) test duplicate notification suppression.
# Duplicate notification suppression is handy in case you run a chat client (like MS-Teams) in more than one
# instance on your machine. In such a case, each instance will send the same desktop notification, turning your machine
# into a "Whac-A-Mole" annoyance.
# Turning on the duplicate suppression will ignore any new notifications which have the same summary and body as
# any visible notifications.

# make backup of user flag setting
SUPPRESSION_BACKUP=$( xfconf-query -c xfce4-notifyd -p /suppress-duplicates )
if [ "$?" == "1" ]
then
  echo "No existing suppression configuration found"
  SUPPRESSION_BACKUP=""
else
  echo "Existing suppression configuration: $SUPPRESSION_BACKUP"
fi

SUMMARY="Test duplicate suppression"

# remove the flag, to test default behaviour
echo "Remove any existing suppression configuration, to test default behaviour (no duplicate suppression)"
xfconf-query -c xfce4-notifyd -p /suppress-duplicates -r

# Big display to make clear how many notifications to expect, as reading tiny fonts while looking at popups is hard ;-)
echo
echo "####### #    # #####  ###### ######"
echo "   #    #    # #    # #      #      "
echo "   #    ###### #    # #####  #####  "
echo "   #    #    # #####  #      #      "
echo "   #    #    # #   #  #      #      "
echo "   #    #    # #    # ###### ###### "
echo

BODY="Test with default 'off', will show 3 of the same notifications now"
echo "$BODY"
notify-send -t 3000 -i info "$SUMMARY" "$BODY"
notify-send -t 3000 -i info "$SUMMARY" "$BODY"
notify-send -t 3000 -i info "$SUMMARY" "$BODY"

sleep 4

# set the flag to "disabled", and test again
echo "Set suppression configuration to 'false', to show no duplicate suppression happens"
xfconf-query -c xfce4-notifyd -p /suppress-duplicates -n -t bool -s false

# Big display to make clear how many notifications to expect, as reading tiny fonts while looking at popups is hard ;-)
echo
echo "####### #    # #####  ###### ######"
echo "   #    #    # #    # #      #      "
echo "   #    ###### #    # #####  #####  "
echo "   #    #    # #####  #      #      "
echo "   #    #    # #   #  #      #      "
echo "   #    #    # #    # ###### ###### "
echo

BODY="Test with suppression 'off', will show 3 of the same notifications now"
echo "$BODY"
notify-send -t 3000 -i info "$SUMMARY" "$BODY"
notify-send -t 3000 -i info "$SUMMARY" "$BODY"
notify-send -t 3000 -i info "$SUMMARY" "$BODY"

sleep 4

# set the flag to "enabled", and now test the real suppression logic
echo "Set suppression configuration to 'true', to show duplicate suppression happens"
xfconf-query -c xfce4-notifyd -p /suppress-duplicates -s true

# Big display to make clear how many notifications to expect, as reading tiny fonts while looking at popups is hard ;-)
echo
echo " ####  #    # ###### "
echo "#    # ##   # #      "
echo "#    # # #  # #####  "
echo "#    # #  # # #      "
echo "#    # #   ## #      "
echo " ####  #    # ###### "
echo

BODY="Test with suppression 'on', will show 1 message out of 3 duplicates"
echo "$BODY"
notify-send -t 3000 -i info "$SUMMARY" "$BODY"
notify-send -t 3000 -i info "$SUMMARY" "$BODY"
notify-send -t 3000 -i info "$SUMMARY" "$BODY"

# restore original flag value
if [ "$SUPPRESSION_BACKUP" == "" ]
then
  echo "Restore backup (remove the suppression configuration)"
   xfconf-query -c xfce4-notifyd -p /suppress-duplicates -r
else
  echo "Restore backup (set suppression configuration to $SUPPRESSION_BACKUP)"
   xfconf-query -c xfce4-notifyd -p /suppress-duplicates -s "$SUPPRESSION_BACKUP"
fi
