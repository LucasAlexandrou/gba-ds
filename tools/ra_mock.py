# Stand-in RetroAchievements server for testing GBA DS offline (see CLAUDE.md):
#   python3 tools/ra_mock.py   (needs Pillow; account tester / secret)
# Minimal stand-in for the RetroAchievements server, for testing GBA DS offline.
import json, sys, io, urllib.parse, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from PIL import Image, ImageDraw
HOST = "http://127.0.0.1:8099"
unlocked = set()
ACH = [
  dict(ID=101, Title="Hangar", Description="100% Goals and Cash in the Hangar with any Skater", Points=5, MemAddr="1=1.300.", BadgeName="00101"),
  dict(ID=102, Title="School II", Description="Get all the gaps in School II", Points=10, MemAddr="1=1.900.", BadgeName="00102"),
  dict(ID=103, Title="Never", Description="Can't be had", Points=25, MemAddr="0=1", BadgeName="00103"),
  dict(ID=104, Title="Slow and steady", Description="Play for a while (a measured achievement: shows progress)", Points=3, MemAddr="M:1=1.5000.", BadgeName="00104"),
  # what the real server adds for clients it doesn't know (GBA DS hides it)
  dict(ID=101000001, Title="Warning: Unknown Emulator", Description="Hardcore unlocks cannot be earned using this emulator.", Points=0, MemAddr="1=1", BadgeName="00000"),
] + [dict(ID=110 + i, Title=f"Filler {i}", Description=f"Locked achievement number {i}, to fill the list and test scrolling", Points=i, MemAddr="0=1", BadgeName=f"0011{i}") for i in range(1, 9)]
log = open("requests.log", "a")
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def reply(self, code, body, ctype="application/json"):
        if isinstance(body, str): body = body.encode()
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        p = urllib.parse.urlparse(self.path)
        if p.path.startswith("/Badge/") or p.path.startswith("/Images/"):
            name = p.path.rsplit("/",1)[1]
            im = Image.new("RGB", (64, 64), (30, 120, 150) if "Images" in p.path else (200, 60, 90))
            d = ImageDraw.Draw(im); d.ellipse((12, 12, 52, 52), fill=(240, 200, 90)); d.text((22, 26), name[:5], fill=(0, 0, 0))
            b = io.BytesIO(); im.save(b, "PNG"); time.sleep(0.3); return self.reply(200, b.getvalue(), "image/png")
        self.handle_rq(urllib.parse.parse_qs(p.query))
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); data = self.rfile.read(n).decode()
        q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query); q.update(urllib.parse.parse_qs(data))
        self.handle_rq(q)
    def handle_rq(self, q):
        r = q.get("r", [""])[0]
        log.write(json.dumps({k: (v[0] if k != "p" else "***") for k, v in q.items()}) + " UA=" + str(self.headers.get("User-Agent")) + "\n"); log.flush()
        if r == "login2":
            if q.get("u", [""])[0] != "tester" or (q.get("p", [""])[0] != "secret" and q.get("t", [""])[0] != "TOKEN123"):
                return self.reply(401, json.dumps({"Success": False, "Error": "Invalid user/password combination. Please try again.", "Code": "invalid_credentials"}))
            return self.reply(200, json.dumps({"Success": True, "User": "tester", "Token": "TOKEN123", "Score": 0, "SoftcoreScore": 40, "Messages": 0, "AvatarUrl": HOST + "/UserPic/tester.png"}))
        if r == "achievementsets":
            achs = [dict(a, Flags=3, Author="me", BadgeURL=f"{HOST}/Badge/{a['BadgeName']}.png", BadgeLockedURL=f"{HOST}/Badge/{a['BadgeName']}_lock.png", Created=1, Modified=1) for a in ACH]
            return self.reply(200, json.dumps({"Success": True, "GameId": 777, "Title": "Tony Hawk's Pro Skater 2", "ConsoleId": 5,
                "ImageIconUrl": HOST + "/Images/054321.png", "RichPresenceGameId": 777, "RichPresencePatch": "",
                "Sets": [{"AchievementSetId": 900, "GameId": 777, "Title": None, "Type": "core", "ImageIconUrl": HOST + "/Images/054321.png", "Achievements": achs, "Leaderboards": []}]}))
        if r == "startsession":
            return self.reply(200, json.dumps({"Success": True, "Unlocks": [{"ID": i, "When": 1} for i in sorted(unlocked)], "HardcoreUnlocks": [], "ServerNow": int(time.time())}))
        if r == "awardachievement":
            unlocked.add(int(q["a"][0]))
            return self.reply(200, json.dumps({"Success": True, "Score": 0, "SoftcoreScore": 50, "AchievementID": int(q["a"][0]), "AchievementsRemaining": len(ACH) - len(unlocked)}))
        if r == "ping":
            return self.reply(200, json.dumps({"Success": True}))
        return self.reply(200, json.dumps({"Success": False, "Error": "unknown request " + r}))
ThreadingHTTPServer(("127.0.0.1", 8099), H).serve_forever()
