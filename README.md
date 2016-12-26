# AMBER

## NAME

amber -- amber-list for incoming mail

## SYNOPSIS

amber [-lnNeE] [-d dir] [-c secs] [-t secs] [-T secs]
[-i secs] [-I secs] [-g file] [-r file] [-p NAME[=VAL]]
[-s NNN  Message ] [command [args]...]

amber -V

## DESCRIPTION

Amber sits in the tcpserver chain for qmail and implements
an "amber list" for incoming mail, not accepting or rejecting
it but deferring connections from new IP addresses for some
time (default five minutes) before it starts accepting mail
from them.

## OPTIONS

<dl>
<dt>-l
<dd>     Log  messages to syslog instead of printing them on
standard error. This is partly  redundant  for  the
normal  configuration  under  qmail  since standard
error will already be logged to syslog. You can use
this  to	take  distinguish between different error
priorities: amber generates  LOG_ERR,  LOG_WARNING,
and LOG_NOTICE.

<dt>-d dir
<dd> Change directory to "dir" first thing.  Amber main-
tains its connection database in the current direc-
tory. It is recommended that the database be pruned
periodically, with a command like find  $dir  -name
'*.t' -mtime +2 -exec rm '{}' ';'

<dt>-t delay-time
<dd>How  long to keep delaying new connections. Time is
seconds, [HH:]MM:SS, or  any  combination  of  DDd,
HHh,  MMm,  and  SSs (eg: 300, 5m, 5:00 are all the
same period, as are 90, 1:30, or 1m30s).

<dt>-T long-delay-time
<dd>Alternate delay to apply to connections from  unre-
solved IP addresses. Default is 6 times delay-time.

<dt>-i idle-time
<dd>Reset connection to idle after this  long.  Accepts
the same time formats as -t

<dt>-I long-idle-time
<dd>Alternate   idle	 reset	time  for  unresolved  IP
addresses. Default is idle-time.

<dt>-p NAME[=VALUE]
<dd>If this variable is set (and has the specied value,
if provided) then amber will pass it without check-
ing. There may be multiple -p options. The  default
value  "AMBERCHECK=NO"  is implicitly in this list,
but additional values  (such  as	"RELAYCLIENT"  or
"RBLSMTPD=") can help avoid embarassment, depending
on your configuration.

<dt>-n<dd>     Throttle	 connections   from    unresolvable    IP
<dd>addresses.   That   is,  after  one  connection  is
allowed, the address is immediately reset to  idle,
throttling the connection to at most once-per-long-
delay-time.

<dt>-N<dd>     Throttles connections  from  domains  that  contain
<dd>strings  that imply the connection is a dynamic IP.
The compiled-in list  is	{"dsl",  "cable",  "dyn",
"ppp", and "dial"}.

<dt>-c connection-delay
<dd>Waits connection-delay seconds before continuing on
to the next stage in the pipeline. This causes some
simplistic spambots and viruses to disconnect.  The
-c option also logs eager-writers  that  send  data
during  the connection delay (legitimate mail soft-
ware is supposed to wait for the HELO before  send-
ing  any	commands) and applies the same delays and
timeouts to them as unresolved IP addresses.

<dt>-e<dd>     Throttle eager writers to one  message  per  delay-
time.

<dt>-E<dd>     Defer eager writers indefinitely.

<dt>-s "NNN Message"
<dd>Specify an alternate SMTP error code to generate on
connection instead  of  the  default  "430  Message
Deferred".

<dt>-b bad-file (v 0.1)
<br>-r redlist-file (v 0.2)
<dd>If  everything else passes, check this file for bad
IP addresses to explicitly block. The  file  format
is  one address per line, optionally followed by an
alternate SMTP error code and  message.  This  file
may be fed from a spamtrap, or statically built, or
created using any other method that makes sense  in
your environment.

<dt>-g greenlist-file (v 0.2)
<dd>After  the connection delay, check this file for IP
addresses to explicitly allow. The file  format  is
one  address  per line. This file may be fed from a
mail  server  for  POP3/IMAP4-before-SMTP,  or  any
other  method  that fits your policies. This should
be a small  file  for  sort-lived  greenlisting  to
avoid  beating on tcpserver's tcp.smtp.cdb file (or
your local equivalent)...  long  term  greenlisting
would be handled before amber.

<dt>-V     Print version and exit.
</dt>

# OUTPUTS
On  success,  run the specified command. On failure, send an
SMTP code back down the socket and close	the  con-
nection.	If  command  is omitted then amber returns a
success or failure status  but  doesn't  send  any-
thing...	normally  a command such as "qmail-smtpd"
would be provided, but this feature could  be  used
if amber is run from a script.

## EXAMPLES

amber -i 1d -p RELAYCLIENT qmail-smtpd

## LICENSE

Amber  is  released  under a "Berkeley" style license.

## AUTHOR

Peter da Silva <resuna at gmail.com>

