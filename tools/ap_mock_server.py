#!/usr/bin/env python3
"""A scripted, deterministic Archipelago server on 127.0.0.1 for offline tests (tools/aplua_regress.py).

No internet and no Archipelago install: Python's standard library only (a minimal RFC 6455 WebSocket
server). It speaks enough of the AP protocol (docs/network protocol.md, AP 0.6.x) for CleriaCore's
client (the Lua mod samples/mods/archipelago_lua) to log in to a two-player room:

    slot 1  "Clicker"   game "Clique"     (a stand-in other world; it owns "Button Activation")
    slot 2  <--slot>    game "Ys Origin"  (CleriaCore), slot_data = tests/ap/test_slot_data.json

The Ys Origin data package is the mod's data/ys_origin.json ids (the apworld's own). Placements are fixed
(PLACEMENTS below; every other location holds the slot's own "SP: 50"). Scripted with chat (Say):

    !give <item name>   Clicker sends that Ys Origin item to the slot (ReceivedItems + PrintJSON)
    !death              a DeathLink from Clicker (Bounced)
    !kick               drop the TCP connection (no close frame)
    !reseed <name>      later connections see another seed_name (the save guard)
    anything else       echoed as a Chat line

Every client command is printed as one "[apserver] <cmd> <json>" line (the password masked), flushed,
so a test can assert on what the game sent.

    python tools/ap_mock_server.py [--port 0] [--slot Yunica] [--password ""] [--seed CLERIATEST]
"""
import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
CLIQUE_ITEM, CLIQUE_LOC = 69696969, 69696969

# location name -> (item name, receiving slot)
PLACEMENTS = {
    "Wailing Blue: 2F Path 1": ("Button Activation", 1),      # the S_1001 chest: another world's item
    "Wailing Blue: 2F Path 1 #2": ("Bronze Key", 2),          # the second chest of that room: our own key
}


def load(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return json.load(f)


class Room:
    def __init__(self, a):
        self.a = a
        table = load("samples/mods/archipelago_lua/data/ys_origin.json")
        self.slot_data = load("tests/ap/test_slot_data.json")
        self.slot_data["death_link"] = bool(a.death_link)
        self.items = {i["name"]: i["id"] for i in table["items"]}
        self.item_flags = {i["name"]: {"progression": 1, "useful": 2, "trap": 4}.get(i["class"], 0) for i in table["items"]}
        self.locs = {l["name"]: l["id"] for l in table["locations"]}
        self.loc_names = {v: k for k, v in self.locs.items()}
        ys = {"item_name_to_id": self.items, "location_name_to_id": self.locs}
        ys["checksum"] = hashlib.sha1(json.dumps(ys, sort_keys=True).encode()).hexdigest()
        self.games = {
            "Ys Origin": ys,
            "Clique": {"item_name_to_id": {"Button Activation": CLIQUE_ITEM},
                       "location_name_to_id": {"The Big Red Button": CLIQUE_LOC}, "checksum": "clique1"},
            "Archipelago": {"item_name_to_id": {"Nothing": -1},
                            "location_name_to_id": {"Cheat Console": -1, "Server": -2}, "checksum": "archipelago1"},
        }
        self.active = list(self.slot_data["location_signals"].values())
        self.received = []        # the slot's ReceivedItems list (kept across reconnects)
        self.checked = set()
        self.lock = threading.Lock()

    def placement(self, loc_id):
        name = self.loc_names.get(loc_id, "")
        item, player = PLACEMENTS.get(name, ("SP: 50", 2))
        if player == 1:
            return {"item": CLIQUE_ITEM, "location": loc_id, "player": 1, "flags": 1, "class": "NetworkItem"}
        return {"item": self.items[item], "location": loc_id, "player": 2, "flags": self.item_flags.get(item, 0),
                "class": "NetworkItem"}


def log(cmd, obj=None):
    if obj is not None and "password" in obj:
        obj = dict(obj, password="***" if obj["password"] else "")
    print("[apserver] %s %s" % (cmd, json.dumps(obj, sort_keys=True, separators=(",", ":")) if obj is not None else ""),
          flush=True)


class Conn:
    def __init__(self, room, sock):
        self.room, self.s = room, sock
        self.tags = []

    # ---- frames ----
    def send(self, packets):
        data = json.dumps(packets).encode()
        n = len(data)
        hdr = bytes([0x81])
        if n < 126:
            hdr += bytes([n])
        elif n < 65536:
            hdr += bytes([126]) + struct.pack(">H", n)
        else:
            hdr += bytes([127]) + struct.pack(">Q", n)
        self.s.sendall(hdr + data)

    def recv_exact(self, n, buf):
        while len(buf) < n:
            d = self.s.recv(65536)
            if not d:
                raise ConnectionError("closed")
            buf += d
        return buf

    def serve(self):
        req = b""
        while b"\r\n\r\n" not in req:
            d = self.s.recv(65536)
            if not d:
                return
            req += d
            if len(req) >= 4 and not req.startswith(b"GET "):
                return                                  # a TLS hello (wss:// tried first): not ours
        key = ""
        for line in req.decode("latin-1").split("\r\n"):
            if line.lower().startswith("sec-websocket-key:"):
                key = line.split(":", 1)[1].strip()
        acc = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        self.s.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        "Sec-WebSocket-Accept: %s\r\n\r\n" % acc).encode())
        log("open")
        self.room_info()
        buf, msg = b"", b""
        while True:
            buf = self.recv_exact(2, buf)
            op, fin = buf[0] & 0x0F, bool(buf[0] & 0x80)
            ln, hl = buf[1] & 0x7F, 2
            if ln == 126:
                buf = self.recv_exact(4, buf)
                ln, hl = struct.unpack(">H", buf[2:4])[0], 4
            elif ln == 127:
                buf = self.recv_exact(10, buf)
                ln, hl = struct.unpack(">Q", buf[2:10])[0], 10
            buf = self.recv_exact(hl + 4 + ln, buf)
            mask, payload = buf[hl:hl + 4], bytearray(buf[hl + 4:hl + 4 + ln])
            buf = buf[hl + 4 + ln:]
            for i in range(len(payload)):
                payload[i] ^= mask[i % 4]
            if op == 8:
                log("close")
                self.s.sendall(bytes([0x88, 2]) + bytes(payload[:2]))
                return
            if op == 9:
                self.s.sendall(bytes([0x8A, len(payload)]) + bytes(payload))
                continue
            if op == 10:
                continue
            msg += payload
            if not fin:
                continue
            for cmd in json.loads(msg.decode("utf-8")):
                if self.on_cmd(cmd) == "kick":
                    log("kick")
                    return
            msg = b""

    # ---- the protocol ----
    def room_info(self):
        r = self.room
        self.send([{"cmd": "RoomInfo", "version": {"major": 0, "minor": 6, "build": 4, "class": "Version"},
                    "generator_version": {"major": 0, "minor": 6, "build": 4, "class": "Version"},
                    "tags": ["AP"], "password": bool(r.a.password),
                    "permissions": {"release": 2, "collect": 2, "remaining": 1}, "hint_cost": 10,
                    "location_check_points": 1, "games": ["Ys Origin", "Clique"],
                    "datapackage_checksums": {g: d["checksum"] for g, d in r.games.items()},
                    "seed_name": r.a.seed, "time": time.time()}])

    def players(self):
        return [{"team": 0, "slot": 1, "alias": "Clicker", "name": "Clicker", "class": "NetworkPlayer"},
                {"team": 0, "slot": 2, "alias": self.room.a.slot, "name": self.room.a.slot, "class": "NetworkPlayer"}]

    def print_send(self, item_part, receiver, finder, loc_id, loc_player):
        r = self.room
        if receiver == finder:
            data = [{"type": "player_id", "text": str(finder)}, {"text": " found their "}, item_part,
                    {"text": " ("}, {"type": "location_id", "text": str(loc_id), "player": loc_player}, {"text": ")"}]
        else:
            data = [{"type": "player_id", "text": str(finder)}, {"text": " sent "}, item_part, {"text": " to "},
                    {"type": "player_id", "text": str(receiver)}, {"text": " ("},
                    {"type": "location_id", "text": str(loc_id), "player": loc_player}, {"text": ")"}]
        self.send([{"cmd": "PrintJSON", "type": "ItemSend", "data": data, "receiving": receiver,
                    "item": {"item": int(item_part["text"]), "location": loc_id, "player": finder,
                             "flags": item_part.get("flags", 0)}}])

    def receive(self, net_items):
        r = self.room
        with r.lock:
            start = len(r.received)
            r.received.extend(net_items)
        self.send([{"cmd": "ReceivedItems", "index": start, "items": net_items}])

    def on_cmd(self, c):
        r = self.room
        cmd = c.get("cmd")
        log(cmd, c)
        if cmd == "GetDataPackage":
            want = c.get("games") or list(r.games)
            self.send([{"cmd": "DataPackage", "data": {"games": {g: r.games[g] for g in want if g in r.games}}}])
        elif cmd == "Connect":
            errs = []
            if c.get("game") != "Ys Origin":
                errs.append("InvalidGame")
            if c.get("name") != r.a.slot:
                errs.append("InvalidSlot")
            if r.a.password and c.get("password") != r.a.password:
                errs.append("InvalidPassword")
            if errs:
                self.send([{"cmd": "ConnectionRefused", "errors": errs}])
                return
            self.tags = c.get("tags", [])
            self.send([{"cmd": "Connected", "team": 0, "slot": 2, "players": self.players(),
                        "missing_locations": [i for i in r.active if i not in r.checked],
                        "checked_locations": sorted(r.checked),
                        "slot_data": r.slot_data if c.get("slot_data", True) else {},
                        "slot_info": {"1": {"name": "Clicker", "game": "Clique", "type": 1, "group_members": [],
                                            "class": "NetworkSlot"},
                                      "2": {"name": r.a.slot, "game": "Ys Origin", "type": 1, "group_members": [],
                                            "class": "NetworkSlot"}},
                        "hint_points": 0},
                       {"cmd": "ReceivedItems", "index": 0, "items": list(r.received)},
                       {"cmd": "PrintJSON", "type": "Join",
                        "data": [{"text": "%s (Team #1) playing Ys Origin has joined." % r.a.slot}], "team": 0, "slot": 2}])
        elif cmd == "ConnectUpdate":
            self.tags = c.get("tags", self.tags)
        elif cmd == "Sync":
            self.send([{"cmd": "ReceivedItems", "index": 0, "items": list(r.received)}])
        elif cmd == "LocationScouts":
            self.send([{"cmd": "LocationInfo", "locations": [r.placement(i) for i in c.get("locations", [])]}])
        elif cmd == "LocationChecks":
            new = [i for i in c.get("locations", []) if i not in r.checked and i in r.active]
            r.checked.update(new)
            if new:
                self.send([{"cmd": "RoomUpdate", "checked_locations": new}])
            for loc in new:
                p = r.placement(loc)
                if p["player"] == 2:
                    self.receive([dict(p, player=2)])
                    self.print_send({"type": "item_id", "text": str(p["item"]), "player": 2, "flags": p["flags"]}, 2, 2, loc, 2)
                else:
                    self.print_send({"type": "item_id", "text": str(p["item"]), "player": 1, "flags": p["flags"]}, 1, 2, loc, 2)
        elif cmd == "StatusUpdate":
            if c.get("status") == 30:
                self.send([{"cmd": "PrintJSON", "type": "Goal",
                            "data": [{"text": "%s (Team #1) has completed their goal." % r.a.slot}], "team": 0, "slot": 2}])
        elif cmd == "Bounce":
            if "DeathLink" in c.get("tags", []) and "DeathLink" in self.tags:
                self.send([dict(c, cmd="Bounced")])
            elif c.get("slots"):
                self.send([dict(c, cmd="Bounced")])
        elif cmd == "Say":
            t = c.get("text", "")
            if t.startswith("!give "):
                name = t[6:].strip()
                if name in r.items:
                    flags = r.item_flags.get(name, 0)
                    self.receive([{"item": r.items[name], "location": CLIQUE_LOC, "player": 1, "flags": flags,
                                   "class": "NetworkItem"}])
                    self.print_send({"type": "item_id", "text": str(r.items[name]), "player": 2, "flags": flags},
                                    2, 1, CLIQUE_LOC, 1)
            elif t == "!death":
                self.send([{"cmd": "Bounced", "tags": ["DeathLink"],
                            "data": {"time": time.time(), "source": "Clicker", "cause": "Clicker pressed the button too hard."}}])
            elif t == "!kick":
                return "kick"
            elif t.startswith("!reseed "):
                r.a.seed = t[8:].strip()          # the next connection's RoomInfo
            else:
                self.send([{"cmd": "PrintJSON", "type": "Chat", "data": [{"text": "%s: %s" % (r.a.slot, t)}],
                            "team": 0, "slot": 2, "message": t}])
        elif cmd == "Get":
            self.send([{"cmd": "Retrieved", "keys": {k: None for k in c.get("keys", [])}}])
        elif cmd == "Set":
            if c.get("want_reply"):
                self.send([{"cmd": "SetReply", "key": c.get("key"), "value": c.get("default"), "original_value": None}])
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--slot", default="Yunica")
    ap.add_argument("--password", default="")
    ap.add_argument("--seed", default="CLERIATEST")
    ap.add_argument("--death-link", action="store_true")
    ap.add_argument("--give", action="append", default=[], help="an item the slot already received at login")
    a = ap.parse_args()
    room = Room(a)
    for name in a.give:
        room.received.append({"item": room.items[name], "location": CLIQUE_LOC, "player": 1,
                              "flags": room.item_flags.get(name, 0), "class": "NetworkItem"})
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", a.port))
    srv.listen(4)
    print("[apserver] listening 127.0.0.1:%d" % srv.getsockname()[1], flush=True)
    while True:
        s, _ = srv.accept()
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        def run(sock=s):
            try:
                Conn(room, sock).serve()
            except (ConnectionError, OSError, ValueError):
                pass
            finally:
                sock.close()
        threading.Thread(target=run, daemon=True).start()


if __name__ == "__main__":
    main()
