#!/usr/bin/env bash

echo "Setting coredump location to /tmp"
echo '/tmp/core_%e.%p' | sudo tee /proc/sys/kernel/core_pattern
ulimit -c unlimited

# run gdb to read the coredump, e.g.
# gdb ../xfce4-notifyd/xfce4-notifyd /tmp/core_xfce4-notifyd.20126

die () {
echo $1
exit $2
}

# create a fresh build to be sure
rm -rf build
meson setup -Cbuild || die "meson setup failed" 4
meson compile -Cbuild || die "meson compile failed" 5

pushd tests

for i in {1..10}; do

  # make sure to kill notifyd in case it's already running in a different version
  # than what we currently want to test
  killall -q xfce4-notifyd;
  # reset the known applications property, to see if values get inserted correctly
  xfconf-query -c xfce4-notifyd -p /applications/known_applications -r;

  ../build/xfce4-notifyd/xfce4-notifyd &
  PID=$!

  # check if notifyd just died on startup
  sleep 1;
  ps -p $PID >/dev/null 2>&1 || die "DIED up front" 1

  # send a notification with some markup
  notify-send "Test nr $i" "This the <a href='https://lmgtfy.com'>dummy notification</a> nr. $i"
  sleep 1;

  # send audio notifications to test icon-only mode
  xdotool key XF86AudioRaiseVolume
  sleep 1;
  xdotool key XF86AudioLowerVolume

  sleep 1;

  # check if the known applications were recorded
  ps -p $PID >/dev/null 2>&1 || die "DIED after send" 2
  if [ $(xfconf-query -c xfce4-notifyd -p /applications/known_applications | wc -l) -eq 0 ]; then
   die "no known application"  3
  fi

  killall -q xfce4-notifyd;
done;
popd
