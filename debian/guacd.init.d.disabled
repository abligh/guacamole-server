#!/bin/sh
### BEGIN INIT INFO
# Provides:          guacd
# Required-Start:    $network $syslog $remote_fs
# Required-Stop:     $network $syslog $remote_fs
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Guacamole proxy daemon
# Description: The Guacamole proxy daemon, required to translate remote desktop protocols into the text-based Guacamole protocol used by the JavaScript application.
### END INIT INFO

# Author: Michael Jumper <zhangmaike@users.sf.net>

PATH=/sbin:/usr/sbin:/bin:/usr/bin
DESC="Guacamole proxy server"
NAME=guacd
DAEMON=/usr/sbin/$NAME
PIDDIR=/var/run/$NAME
PIDFILE=$PIDDIR/$NAME.pid
DAEMON_ARGS="-p $PIDFILE"
SCRIPTNAME=/etc/init.d/$NAME

# Ensure $HOME is set properly, even if environment is clear
USER=`whoami`
export HOME=`sh -c "echo ~$USER"`

# Exit if the package is not installed
[ -x "$DAEMON" ] || exit 0

# Read configuration variable file if it is present
[ -r /etc/default/$NAME ] && . /etc/default/$NAME

# Define LSB log_* functions.
# Depend on lsb-base (>= 3.0-6) to ensure that this file is present.
. /lib/lsb/init-functions

# Add listen port, if given
if [ -n "$LISTEN_PORT" ]; then
    DAEMON_ARGS="$DAEMON_ARGS -l $LISTEN_PORT"
fi

# Add listen address, if given
if [ -n "$LISTEN_ADDRESS" ]; then
    DAEMON_ARGS="$DAEMON_ARGS -b $LISTEN_ADDRESS"
fi

#
# Function that starts the daemon/service
#
do_start()
{
    # Return
    #   0 if daemon has been started
    #   1 if daemon was already running
    #   2 if daemon could not be started

    # Make PID directory, if it doesn't already exist
    mkdir -p "$PIDDIR"
    chown guacd:guacd "$PIDDIR"

    # Test whether aleady running
    start-stop-daemon --start --quiet --pidfile $PIDFILE \
        --exec $DAEMON --test > /dev/null || return 1

    # Attempt to start
    start-stop-daemon --start --quiet --pidfile $PIDFILE \
        --exec $DAEMON --chuid guacd:guacd -- $DAEMON_ARGS 2> /dev/null \
        || return 2

}

#
# Function that stops the daemon/service
#
do_stop()
{
    # Return
    #   0 if daemon has been stopped
    #   1 if daemon was already stopped
    #   2 if daemon could not be stopped
    #   other if a failure occurred

    start-stop-daemon --stop --quiet --retry=TERM/30/KILL/5 \
        --pidfile $PIDFILE --name $NAME

    RETVAL="$?"
    [ "$RETVAL" = 2 ] && return 2

    # Wait for children to finish too if this is a daemon that forks
    # and if the daemon is only ever run from this initscript.
    # If the above conditions are not satisfied then add some other code
    # that waits for the process to drop all resources that could be
    # needed by services started subsequently.  A last resort is to
    # sleep for some time.
    start-stop-daemon --stop --quiet --oknodo --retry=0/30/KILL/5 \
        --exec $DAEMON

    [ "$?" = 2 ] && return 2

    # Many daemons don't delete their pidfiles when they exit.
    rm -f $PIDFILE
    return "$RETVAL"

}

case "$1" in
  start)
    log_daemon_msg "Starting $DESC" "$NAME"
    do_start
    case "$?" in
        0|1) log_end_msg 0 ;;
        2)   log_end_msg 1 ;;
    esac
    ;;
  stop)
    log_daemon_msg "Stopping $DESC" "$NAME"
    do_stop
    case "$?" in
        0|1) log_end_msg 0 ;;
        2)   log_end_msg 1 ;;
    esac
    ;;
  status)
       status_of_proc "$DAEMON" "$NAME" && exit 0 || exit $?
       ;;
  restart|force-reload)
    #
    # If the "reload" option is implemented then remove the
    # 'force-reload' alias
    #
    log_daemon_msg "Restarting $DESC" "$NAME"
    do_stop
    case "$?" in
      0|1)
        do_start
        case "$?" in
            0) log_end_msg 0 ;;
            1) log_end_msg 1 ;; # Old process is still running
            *) log_end_msg 1 ;; # Failed to start
        esac
        ;;
      *)
        # Failed to stop
        log_end_msg 1
        ;;
    esac
    ;;
  *)
    echo "Usage: $SCRIPTNAME {start|stop|status|restart|force-reload}" >&2
    exit 3
    ;;
esac

:
