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
from __future__ import print_function
del print_function

UDRONE_ADDR = ("239.6.6.6", 21337)
UDRONE_GROUP_DEFAULT = "!all-default"
UDRONE_MAX_DGRAM = 32 * 1024
UDRONE_RESENT_STRATEGY = [0.5, 1, 1]
UDRONE_IDLE_INTVAL = 19

class DroneNotReachableError(EnvironmentError):
	pass
class DroneNotFoundError(EnvironmentError):
	pass
class DroneRuntimeError(EnvironmentError):
	pass
class DroneConflict(EnvironmentError):
	pass

import logging
class NullHandler(logging.Handler):
	def emit(self, record):
		pass

logger = logging.getLogger("udrone")
logger.setLevel(logging.DEBUG)
logger.addHandler(NullHandler())
del logging, NullHandler

class DroneGroup(object):
	def __init__(self, host, groupid):
		self.host = host
		self.groupid = groupid
		self.idle_intval = UDRONE_IDLE_INTVAL
		self.timer = None
		self._timer_setup()
		self.seq = self.host.genseq()
		self.members = set()
		logger.debug("Group %s created.", self.groupid)

	def _timer_action(self):
		logger.debug("Group %s keep-alive timer triggered", self.groupid)
		if len(self.members) > 0:
			self.host.whois(self.groupid, need = 0, seq = 0)
		self._timer_setup()

	def _timer_setup(self):
		import threading
		if self.timer:
			self.timer.cancel()
		self.timer = threading.Timer(self.idle_intval, self._timer_action)
		self.timer.setDaemon(True)
		self.timer.start()

	def assign(self, max, min = None, board=None):
		from errno import ENOENT
		if not min:
			min = max if max else 1
		avail = self.host.whois(UDRONE_GROUP_DEFAULT, max, board=board)[:max]
		if len(avail) < min:
			raise DroneNotFoundError((ENOENT, "You must construct additional drones"))
		newmem = self.engage(avail)
		if len(newmem) < min:
			max -= len(newmem)
			avail = self.host.whois(UDRONE_GROUP_DEFAULT, max)[:max]
			newmem += self.engage(avail)
		if len(newmem) < min:
			if len(newmem) > 0: # Rollback
				self.host.call_multi(newmem, None, "!reset", None, "status")
			raise DroneNotFoundError((ENOENT, "You must construct additional drones"))
		return newmem

	def engage(self, nodes):
		data = {"group": self.groupid, "seq": self.seq}
		ans = self.host.call_multi(nodes, None, "!assign", data, "status")
		members = []
		for member, answer in ans.iteritems():
			try:
				if answer["data"]["code"] == 0:
					members.append(member)
			except Exception:
				pass
		self.members |= set(members)
		return members

	def reset(self, reset = None):
		from errno import ETIMEDOUT
		if len(self.members) < 1:
			return
		expect = self.members.copy()
		self.host.reset(self.groupid, reset, expect)
		self.members = expect
		if (len(expect) > 0):
			raise DroneNotReachableError((ETIMEDOUT, "Request Timeout", expect))

	def request(self, type, data = None, timeout = 60):
		import time
		from errno import ENOENT
		if len(self.members) < 1:
			raise DroneNotFoundError((ENOENT, "Drone group is empty"))
		if type[0] != '!':
			self.seq += 1
			seq = self.seq
		else:
			seq = self.host.genseq()

		pending = self.members.copy()
		i = 0
		answers = {}
		start = time.time()
		now = start
		self._timer_setup()

		while (len(pending) > 0
		and (now - start) >= 0 and (now - start) < timeout):
			expect = pending.copy()
			i += 1
			if i % 2 == 1:
				answers.update(self.host.call(self.groupid,
					seq, type, data, expect = expect))
			else:
				self.host.recv_until(answers, seq, expect = expect,
					timeout = min(10, timeout - (now - start)))

			for drone in expect: # Timed out
				answers[drone] = None
			for drone, ans in answers.iteritems():
				if ans and ans["type"] == "accept":
					answers[drone] = None # In Progress
				elif drone in pending and ans != None:
					pending.remove(drone)
			now = time.time()
			self._timer_setup()
		return answers

	def call(self, type, data = None, timeout = 60, update = None):
		from errno import ETIMEDOUT, EOPNOTSUPP, EPROTO
		res = self.request(type, data, timeout)
		if update:
			update.update(res)
		for drone, answer in res.iteritems():
			if not answer: # Some drone didn't answer
				raise DroneNotReachableError((ETIMEDOUT, "Request Timeout", [drone]))
			if not drone in self.members: # Some unknown drone answered
				raise DroneConflict([drone])
			if answer["type"] == "unsupported":
				raise DroneRuntimeError((EOPNOTSUPP, "Unknown Command", drone))
			try:
				if answer["type"] == "status" and answer["data"]["code"] > 0:
					errstr = None
					if "errstr" in answer["data"]:
						errstr = answer["data"]["errstr"]
					raise DroneRuntimeError((answer["data"]["code"], errstr, drone))
			except Exception as e:
				if isinstance(e, DroneRuntimeError):
					raise e
				else:
					raise DroneRuntimeError((EPROTO, "Invalid Status Reply", drone))
		return update if update else res


class DroneHost(object):
	def __init__(self, local = None, args = []):
		import socket, select, os, binascii

		self.args = args
		self.hostid = binascii.hexlify(os.urandom(3))
		self.uniqueid = "Host" + self.hostid
		self.addr = UDRONE_ADDR
		self.resent = UDRONE_RESENT_STRATEGY
		self.maxsize = UDRONE_MAX_DGRAM

		self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		self.socket.bind(('', 0))
		if local:
			self.socket.setsockopt(socket.SOL_IP, socket.IP_MULTICAST_IF,
				socket.inet_aton(local))
		self.socket.setblocking(0)

		self.poll = select.poll()
		self.poll.register(self.socket, select.POLLIN)

		self.groups = []
		logger.info("Initialized host with ID: " + self.uniqueid)

	def genseq(self):
		import struct, os
		return struct.unpack("=I", os.urandom(4))[0] % 2000000000

	def send(self, to, seq, type, data = None):
		import json
		msg = {"from": self.uniqueid, "to": to, "type" : type, "seq": seq}
		if data != None:
			msg["data"] = data
		packet = json.dumps(msg, separators = (',', ':'))
		logger.debug("Sending: %s", packet)
		self.socket.sendto(packet, self.addr)

	def recv(self, seq, type = None):
		import socket, json
		from errno import EWOULDBLOCK
		while True:
			try:
				msg = json.loads(self.socket.recv(self.maxsize))
				if (msg["from"] and msg["type"] and msg["to"] == self.uniqueid
				and (not type or msg["type"] == type)
				and (not seq or msg["seq"] == seq)):
					logger.debug("Received: %s", str(msg))
					return msg
			except Exception as e:
				if isinstance(e, socket.error) and e.errno == EWOULDBLOCK:
					return None

	def recv_until(self, answers, seq, type = None, timeout = 1, expect = None):
		import time
		logger.debug("Receiving replies for seq %i for %.1f secs expecting %s",
			seq, timeout, expect)
		start = time.time()
		now = start
		while ((now - start) >= 0 and (now - start) < timeout
		and (expect == None or len(expect) > 0)):
			self.poll.poll((timeout - (now - start)) * 1000)
			while True:
				msg = self.recv(seq, type)
				if msg:
					answers[msg["from"]] = msg
					if expect != None and msg["from"] in expect:
						expect.remove(msg["from"])
				elif not msg:
					break
			now = time.time()

	def call(self, to, seq, type, data = None, resptype = None, expect = None):
		if not seq:
			seq = self.genseq()
		answers = {}
		for timeout in self.resent:
			self.send(to, seq, type, data)
			self.recv_until(answers, seq, resptype, timeout, expect)
			if expect != None and len(expect) == 0:
				break
		return answers

	def call_multi(self, nodes, seq, type, data = None, resptype = None):
		if not seq:
			seq = self.genseq()
		answers = {}
		for timeout in self.resent:
			for node in nodes:
				self.send(node, seq, type, data)
			self.recv_until(answers, seq, resptype, timeout, nodes)
			if len(nodes) == 0:
				break
		return answers

	def whois(self, group, need = None, seq = None, board = None):
		answers = {}
		if seq == None:
			seq = self.genseq()
		for timeout in self.resent:
			data = None
			if board is not None:
				data = board
			self.send(group, seq, "!whois", data)
			if need == 0:
				break
			self.recv_until(answers, seq, "status", timeout)
			if need and len(answers) >= need:
				break
		return answers.keys()

	def reset(self, whom, how = None, expect = None):
		data = {"how": how} if how else None
		return self.call(whom, None, "!reset", data, "status", expect)

	def Group(self, groupid, absolute = False):
		if not absolute:
			groupid += self.hostid
		if (len(groupid) > 16):
			raise IndexError()
		group = DroneGroup(self, groupid)
		self.groups.append(group)
		return group

	def disband(self, reset = None):
		for group in self.groups:
			group.reset(reset)
		self.groups = []

	def execfile(self, path):
		execfile(path, {"self": self})

if __name__ == "__main__":
	from getopt import gnu_getopt
	from sys import argv

	source = ""
	debug = False

	optlist, args = gnu_getopt(argv[1:], "s:d")
	for key, val in optlist:
		if key == "-s":
			source = val
		elif key == "-d":
			debug = True

	del argv, gnu_getopt, optlist

	import logging
	logcons = logging.StreamHandler()
	if not debug:
		logcons.setLevel(logging.WARNING)
	logging.getLogger("udrone").addHandler(logcons)
	del debug, logging

	if len(source) < 1:
		print("!!! WARNING: You haven't set a source address !!!",
			  "\nIf you cannot communicate with drones set the source address",
			  "\nto a locally configured address of the local network interface",
			  "\nthe drones are attached to (e.g. -s192.168.10.2).",
			  "\nAlso make sure drones can reach this address via unicast.\n")

	self = DroneHost(source if len(source) > 0 else None, args = args[1:])
	del DroneHost, source

	from functools import partial
	from atexit import register

	def teardown(host):
		try:
			host.disband()
		except Exception:
			pass
	register(partial(teardown, self))
	del register, partial, teardown

	if len(args) > 0:
		self.execfile(args[0])
	else:
		from os import environ
		environ["PYTHONINSPECT"] = "1"
		del environ, args
		import readline
		del readline # Makes perfect sense, haha !

		print("Welcome to the udrone interactive Python shell!")
		print("\nudrone Commands:")
		print("self.whois(target)		# Send an echo-request")
		print("self.reset(target, <'system'>)	# Reset nodes ('system' requests reboot)")
		print("group = self.Group(prefix)	# Create new group (prefix length <= 10)")
		print("self.execfile(path)		# Execute a script")
		print("\nudrone Group Commands:")
		print("group.assign(max, min = max)	# Assign a number of idle nodes")
		print("group.engage([node, node, ...])	# Invite nodes by ID")
		print("group.call(command, <data>)	# Send group-request and return replies")
		print("group.reset(<'system'>)		# Disband group by resetting nodes")
		print("\nScanning for idle drones...")
		idle = self.whois("!all-default")
		if len(idle) == 0:
			print("No drones found!")
		else:
			print("Found:", ", ".join(idle))
		del idle
		print("\nNow have fun...")
