#!/usr/bin/python
'''
udrone - Multicast Device Remote Control 
Copyright (C) 2010 Steven Barth <steven@midlink.org>
Copyright (C) 2010-2019 John Crispin <john@phrozen.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
'''
import json
import time
import signal
import os
import sys
import conf

import socket
import fcntl
import struct

import re

# netlink helper for reading a netdevs ip
def get_ip_address(ifname):
	s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	return socket.inet_ntoa(fcntl.ioctl(
		s.fileno(),
		0x8915,  # SIOCGIFADDR
		struct.pack('256s', ifname[:15])
		)[20:24])

# bring up drone host
from udrone import DroneHost
host = DroneHost(get_ip_address(conf.conf["ifname"]))
drone = []

# this function replaces values inside parameters based on conf.py 
def replace_tags(s, c):
	m = re.compile('(\$\w+)')
	ret = json.dumps(s)
	for tag in re.findall(m, ret):
		if tag != '$iterate':
			ret = ret.replace(tag, conf.conf[tag[1:]])
		elif c is not None:
			ret = ret.replace(tag, "%d"%c)

	return json.loads(ret)

# this helper is used to send a command to a drone
def cmd_drone(v, c):
	# per default we wait 10s for a reply
	timeout = 10
	# if a timeout value was passed, use it instead of the default
	if (len(v) == 5):
		timeout = v[4]
	# check if the call has a payload
	if (len(v) < 4):
		# no payload so do a flat call
		print("DRONE calling \"" + v[2] + "\"")
		return drone[v[1] - 1].call(v[2])
	else:
		# there is a payload, substitue global vars and iteration coutners
		payload = replace_tags(v[3], c)
		# issue the actual command
		print("DRONE calling \"" + v[2] + "\":"+ json.dumps(payload))
		return drone[v[1] - 1].call(v[2], payload, timeout)


# this command sleeps for N seconds
def cmd_sleep(v, c):
	print("SLEEP %d"%v[1])
	time.sleep(v[1])

# this command prints a comment inside the log
def cmd_comment(v, c):
	com = replace_tags(v[1], c);
	print("COMMENT %s"%com)

# this command can be prepended to any other command. it simply inverts the result
def cmd_fail(v, c):
	if(cmd_map[v[1]] is not None):
		try:
			cmd_map[v[1]](v[1:], c)
		except:
			print("INVERT we expected this command to fail which it did");
			return 0
		print("ERROR command was supposed to fail")
		raise ExceptionClass(1000, "command should have failed", "foo")

# this command executes a shell script. if the script returns 0 we assume all is well
def cmd_script(v, c):
	path = v[1]
	param = replace_tags(v[2], c)
	print("SCRIPT " + path + " " + param)
	ret = os.system(path + " " + param)
	if (ret):
		raise ExceptionClass(1000, "script returned %d"%ret, "foo")

# this is the map of all complex call helpers
cmd_map = {
	"drone":cmd_drone,
	"sleep":cmd_sleep,
	"fail":cmd_fail,
	"comment":cmd_comment,
	"script":cmd_script
}

# this function runs a actual test
def run_test(test):
	max = 1
	loop = 0

	# we can iterate  0->max
	try:
		if(test["repeat"] is not None):
			max = test["repeat"]
	except:
		max = 1

	# or we iterate 0/first -> last
	try:
		if(test["last"] is not None):
			max = test["last"]
		if(test["first"] is not None):
			loop = test["first"]
			loop = loop - 1
	except:
		loop = 0

	# do $max iterations of the test set
	fail = 0
	while(loop < max):
		loop = loop + 1
		print("RUN \"" +  test["desc"] + "\" - iteration %d"%loop)
		try:
			# loop over all commands and call them
			for c in test["cmd"]:
				res = cmd_map[c[0]](c, loop)
		except KeyboardInterrupt as e:
			# ctrl-C was hit
			exit(-1)
		except:
			# increment fail counter
			fail = fail + 1
			print("FAIL iterate %d"%loop)
			print(sys.exc_info())
	if(fail > 0):
		raise ExceptionClass(1000, "%d iterations failed"%fail, "foo")

if(len(sys.argv) < 2):
	print("you need to tell me what to do")
	exit(-1)

try:
	f = open(sys.argv[1], 'r')
	buf = f.read()
	f.close()
	test = json.loads(buf)
	print('START "' + test["id"] + '" - "' + test["desc"] + '"')
	if test["drones"] is None:
		print("ERROR no drones defined")
		exit(-1)
except:
	print(sys.exc_info())
	print("ERROR bad json")
	exit(-1)

drone_count = 0
try:
	for l in test["drones"]:
		count = l
		board = None
		if type(l) == list:
			count = l[0]
			board = l[1]
		d = 0
		while (d < count):
			print("DRONE init unit %d"%drone_count)
			drone.append(host.Group("Drone%d"%drone_count))
			drone[drone_count].assign(1, board = board)
			d = d + 1
			drone_count = drone_count + 1

except:
	print("ERROR failed to grab drones")
	print(sys.exc_info())
	exit(-1)

# iterate over the tests
success = 0
count = 0
for t in test["test"]:
	count = count + 1
	try:
		run_test(t)
		print("PASS %d "%count + test["id"])
		success = success + 1
		if (t["sleep"]):
			print("SLEEP %d"%t["sleep"])
			time.sleep(t["sleep"])
	except:
		print("FAIL exception running %d "%count + test["id"])
		print(sys.exc_info())
		time.sleep(5)
d = 0
while (d < drone_count):
	print("DRONE reset unit %d"%d)
	cmd_drone(["DRONE", d, "!reset"], 1)
	d = d + 1

result = "FAIL"
if(success == len(test["test"])):
	result = "PASS"

print("RESULT " + result + " %d/%d"%(success, len(test["test"])))

exit(0)
