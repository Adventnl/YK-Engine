#!/usr/bin/env python3
"""Author Blackwater: a connected prison, social trades, and a repair escape route.

All gameplay is standard YK scene/dialogue data. Labels are in the running game,
so the native player capture is also the annotated overview (no mock screenshot).
"""
import json
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ENTITIES = []


def c(kind, **props):
    return {"type": kind, "properties": props}


def add(name, x, y, *parts, tags=None):
    eid = f"{len(ENTITIES) + 1:016x}"
    item = {"id": eid, "name": name,
            "transform": {"position": [x, y], "rotation": 0, "scale": [1, 1]},
            "components": list(parts)}
    if tags:
        item["tags"] = tags
    ENTITIES.append(item)
    return eid


def sprite(w, h, color, layer=0, **props):
    return c("SpriteRenderer", size=[w, h], color=color, layer=layer, **props)


def box(w, h, layer="Solid", trigger=False, **props):
    return c("Collider", shape="Box", size=[w, h], layer=layer, isTrigger=trigger, **props)


def rect(name, x, y, w, h, color, layer=0, solid=False):
    return add(name, x, y, sprite(w, h, color, layer), *([box(w, h)] if solid else []))


def furnishing(name, x, y, w, h, color):
    rect(name + " shadow", x + .12, y + .16, w + .12, h + .12, "#101c2470", 1)
    rect(name, x, y, w, h, "#172832ff", 3, True)
    rect(name + " top", x, y - .08, w - .14, h - .18, color, 4)
    rect(name + " edge", x, y + h / 2 - .14, w - .12, .1, "#c3cbb744", 5)


def wall(name, x, y, w, h):
    rect(name + " shade", x + .09, y + .15, w, h, "#101b25ff", 5)
    rect(name, x, y, w, h, "#617d83ff", 6, True)
    rect(name + " cap", x - .04, y - .04, max(.1, w - .1), max(.1, h - .1), "#9eafa4ff", 7)


def horizontal(name, y, x0, x1, door=None, width=2.4):
    if door is None:
        wall(name, (x0+x1)/2, y, x1-x0, .42)
    else:
        for a, b in [(x0, door-width/2), (door+width/2, x1)]:
            wall(name + str(a), (a+b)/2, y, b-a, .42)
        rect(name + " threshold", door, y, width, .52, "#b4ac83ff", -2)


def vertical(name, x, y0, y1, door=None, width=2.4):
    if door is None:
        wall(name, x, (y0+y1)/2, .42, y1-y0)
    else:
        for a, b in [(y0, door-width/2), (door+width/2, y1)]:
            wall(name + str(a), x, (a+b)/2, .42, b-a)
        rect(name + " threshold", x, door, .52, width, "#b4ac83ff", -2)


def text(name, message, x, y, scale=1.65, color="#eef0d7ff", layer=22):
    return add(name, 0, 0, c("UiText", text=message, anchor="TopLeft", offset=[x, y],
                            scale=scale, color=color, shadow=True, layer=layer))


def panel(name, x, y, w, h, color="#122430ee", layer=20):
    add(name, 0, 0, c("UiPanel", anchor="TopLeft", offset=[x, y], size=[w, h], color=color, layer=layer))


def label(name, message, x, y, scale=1.55, color="#eef0d7ff"):
    # Fixed overview camera: 1600 x 1000, 36 world units high, centered at (26, 14).
    return text(name, message, (x + 2.8) * 1000/36, (y + 4) * 1000/36, scale, color)


def room(name, title, subtitle, x0, y0, x1, y1, color):
    rect(name + " floor", (x0+x1)/2, (y0+y1)/2, x1-x0, y1-y0, color, -25)
    # Tile joints are material, not a background UI grid.
    for i in range(int(x0)+1, int(x1), 2):
        rect(name + f" tile x{i}", i, (y0+y1)/2, .025, y1-y0-.5, "#d2d8bb0e", -24)
    for i in range(int(y0)+1, int(y1), 2):
        rect(name + f" tile y{i}", (x0+x1)/2, i, x1-x0-.5, .025, "#d2d8bb0e", -24)
    label(name + " title", title, x0+.65, y0+.7, 1.95)
    label(name + " subtitle", subtitle, x0+.65, y0+1.4, 1.35, "#d2dec9ff")


def action(name, x, y, w, h, color, prompt, flag="", required="", hide=False, extra=()):
    rect(name + " highlight", x, y, w+.28, h+.28, "#edca7938", 7)
    return add(name, x, y, sprite(w, h, color, 8),
               c("Interactable", prompt=prompt, range=1.35, setFlag=flag,
                 requiredFlag=required, once=bool(flag), hideWhenUsed=hide), *extra)


def gate(name, x, y, horizontal_gate, flag, prompt):
    w, h = (2.4, .44) if horizontal_gate else (.44, 2.4)
    eid = add(name, x, y, sprite(w, h, "#d2a257ff", 9), box(w, h),
              c("StateGate", startsLocked=True, unlockFlag=flag,
                persistFlag=name.lower().replace(" ", "_")+"_open", animationSeconds=.3),
              c("Interactable", prompt=prompt, range=1.6))
    # Fixed jambs; the actual gate disappears when opened, including its collider.
    for side in (-1, 1):
        rect(name + f" jamb {side}", x+(side*1.25 if horizontal_gate else 0),
             y+(0 if horizontal_gate else side*1.25), .25, .25, "#e4d6a4ff", 10)
    return eid


def setvar(name, value=1):
    return {"type": "SetVariable", "name": name, "value": value}


def graph(name, nodes):
    path = ROOT / "assets/dialogue" / (name + ".ykdialogue")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"version": 1, "start": "start", "nodes": nodes}, indent=2)+"\n")
    return "assets/dialogue/" + name + ".ykdialogue"


def trade(name, speaker, item, reward, intro, after):
    return graph(name, {
        "start": {"branch": [{"if": {"var": reward}, "next": "done"}, {"next": "offer"}]},
        "offer": {"speaker": speaker, "text": intro, "choices": [
            {"text": f"Trade the {item}.", "if": {"var": item}, "next": "trade"},
            {"text": "I'll come back."}]},
        "trade": {"speaker": speaker, "text": after,
                  "actions": [setvar(item, 0), setvar(reward)]},
        "done": {"speaker": speaker, "text": after}})


# Tiny authored pixel sprites: opaque suit, skin, hair, eyes, hands and boots.
# Exact rectangular geometry, nearest-neighbour sampling; no external art dependency.
def character_asset(name, suit, hair, guard=False):
    im = Image.new("RGBA", (16, 24))
    d = ImageDraw.Draw(im)
    d.ellipse((2,20,14,23), fill="#101a2680")
    d.rectangle((4,13,6,21), fill="#24313d")
    d.rectangle((9,13,11,21), fill="#24313d")
    d.rectangle((3,21,6,22), fill="#14202b")
    d.rectangle((9,21,12,22), fill="#14202b")
    d.rectangle((3,8,12,16), fill=suit)
    d.rectangle((4,8,5,15), fill="#ffffff24")
    d.rectangle((2,9,3,15), fill=suit)
    d.rectangle((12,9,13,15), fill=suit)
    d.rectangle((2,15,3,17), fill="#edbc89")
    d.rectangle((12,15,13,17), fill="#edbc89")
    d.rectangle((6,7,9,9), fill="#d49b70")
    d.rectangle((4,2,11,7), fill="#edbc89")
    d.rectangle((3,0,12,3), fill=hair)
    d.rectangle((4,3,4,4), fill=hair)
    d.rectangle((6,4,6,4), fill="#172735")
    d.rectangle((10,4,10,4), fill="#172735")
    if guard:
        d.rectangle((3,1,12,3), fill="#22374b")
        d.rectangle((3,3,13,3), fill="#d9c07a")
        d.rectangle((10,10,11,11), fill="#f3d386")
        d.rectangle((4,15,11,15), fill="#182838")
    path=ROOT / "assets/characters" / (name+".png")
    path.parent.mkdir(parents=True, exist_ok=True)
    im.save(path)
    return "assets/characters/"+name+".png"


orange = character_asset("inmate", "#d29152", "#4a342d")
player_tex = character_asset("runner", "#f0b85d", "#443130")
blue = character_asset("guard", "#52788e", "#283441", True)
medic = character_asset("medic", "#b8d6c3", "#654435")


def npc(name, x, y, texture, message=None, sequence=None, flag="", waypoints=()):
    parts = [sprite(.95, 1.43, "#ffffffff", 12, texture=texture, offset=[0,-.35], ySort=True),
             c("RigidBody", type="Dynamic", gravityScale=0, fixedRotation=True, allowSleep=False),
             box(.48,.42,"Prop",offset=[0,.26]),
             c("NpcPath", waypoints=list(waypoints), speed=1.15, waitSeconds=1.4),
             c("Interactable", prompt="E  Talk to "+name, range=1.5, setFlag=flag),
             c("Dialogue", speaker=name, sequence=sequence or "", pages=[message] if message else [],
               charactersPerSecond=0)]
    return add(name,x,y,*parts)


# Process the interactor before NPC dialogue updates, so E closes a conversation
# without reopening it later in the same fixed tick. Entity ordering is intentional.
player=add("Runner",3.7,4.6,
    sprite(.95,1.43,"#ffffffff",13,texture=player_tex,offset=[0,-.35],ySort=True),
    c("RigidBody",type="Dynamic",gravityScale=0,linearDamping=8,fixedRotation=True,allowSleep=False),
    box(.48,.42,"Player",offset=[0,.26]),c("PlayerInput",actionSet="Player1"),
    c("TopDownController",speed=5.2,acceleration=45),c("Interactor",action="Interact"),tags=["player"])

rect("Foundation",26,14,52,28,"#142430ff",-30)
room("Cell block", "CELL BLOCK A", "START / ASK MILO FOR THE CODE",0,0,12,28,"#354b55ff")
room("Canteen", "01  CANTEEN", "TAKE A SNACK FOR TESS",12,0,25,11,"#4a5550ff")
room("Laundry", "02  LAUNDRY", "TESS: SNACK > FUSE",25,0,37,11,"#3f565aff")
room("Security", "05  SECURITY", "CODE > YARD ACCESS",37,0,46,11,"#364b5aff")
room("Utility", "UTILITY", "STORES / NO EXIT",46,0,52,11,"#3d4a50ff")
room("Workshop", "04  WORKSHOP", "FUSE + TOOL > RELAY",12,17,25,28,"#4b4a40ff")
room("Infirmary", "03  INFIRMARY", "BANDAGE FOR REX",25,17,37,28,"#3d5750ff")
room("Yard", "EXERCISE YARD", "CROSS TO MAINTENANCE",37,11,46,28,"#455447ff")
room("Maintenance", "06  REPAIR", "RELAY > POWER",46,11,52,21,"#4b5046ff")
room("Exit lane", "07  EXIT", "BUTTON > GATE",46,21,52,28,"#335547ff")
rect("Central corridor",24.5,14,25,6,"#526055ff",-22)
label("Corridor name","ASSOCIATION HALL / CONNECTS ALL ROOMS",14,12,1.65,"#dae1c7ff")
for x in range(14,36,3):
    rect("Corridor dash "+str(x),x,15.9,.8,.08,"#b1b99888",-20)
# Boundaries, room divisions, and generous two-way door openings.
horizontal("North perimeter",0,0,52)
horizontal("South perimeter",28,0,52)
vertical("West perimeter",0,0,28)
vertical("East perimeter",52,0,28)
vertical("Cell block east",12,0,28,14)
for title,y,x0,x1,door in [
    ("Canteen south",11,12,25,18.5),("Laundry south",11,25,37,31),
    ("Security south",11,37,46,None), ("Workshop north",17,12,25,18.5),
    ("Infirmary north",17,25,37,31), ("Utility south",11,46,52,None),
    ("Maintenance south",21,46,52,49)]:
    horizontal(title,y,x0,x1,door)
vertical("Canteen laundry wall",25,0,11)
vertical("Laundry security wall",37,0,11,7)
vertical("Security utility wall",46,0,11,6)
vertical("Workshop infirmary wall",25,17,28)
vertical("Infirmary yard wall",37,17,28)
vertical("Yard entrance",37,11,17,14)
vertical("Maintenance west",46,11,21,14.5)
vertical("Exit west",46,21,28)
# Cells occupy both ends; open cell thresholds join the communal wing.
for y in (7,21):
    horizontal("Cell front west "+str(y),y,0,6,3)
    horizontal("Cell front east "+str(y),y,6,12,9)
for y0,y1 in [(0,7),(21,28)]:
    vertical("Cell partition "+str(y0),6,y0,y1,4 if y0==0 else 24)
for y in (7,21):
    for x in [1,5,7,11]:
        rect(f"Cell bars {x}-{y}",x,y,.10,.72,"#c8d1b6ff",8)
# Beds, toilet blocks, a common table, and lockers.
for x,y in [(2.5,3.3),(9.3,3.3),(2.5,24.1),(9.3,24.1)]:
    furnishing(f"Bunk {x}-{y}",x,y,2.4,1.25,"#7c9390ff")
    rect(f"Bunk pillow {x}-{y}",x-.75,y-.1,.5,.75,"#d0d7beff",5)
    rect(f"Bunk blanket {x}-{y}",x+.45,y,1.0,1.0,"#657d7dff",5)
for x,y in [(4.6,5.5),(10.5,5.5),(4.6,26.4),(10.5,26.4)]:
    furnishing(f"Toilet {x}-{y}",x,y,.7,.9,"#afbeb1ff")
furnishing("Common table",4,11,3,1.4,"#8a7860ff")
for x in (3,5):
    furnishing("Common stool "+str(x),x,12.3,.65,.65,"#78857aff")
for y in (15,17.5):
    furnishing("Cell locker "+str(y),2,y,1.2,1.6,"#657c80ff")
label("Player start label","YOU / START",1.25,4.8,1.6,"#ffdb8fff")
label("Milo label","MILO [E]\nACCESS CODE",7.0,16.4,1.45,"#ffdb8fff")
# Canteen kitchen and communal seating, with the snack on an accessible counter edge.
furnishing("Serving counter",18.5,3.5,9.5,1.1,"#91a096ff")
for x in (15.2,17.3,19.4,21.5):
    rect("Serving tray "+str(x),x,3.4,1.1,.5,"#bac1a0ff",5)
for x in (16,21.5):
    furnishing("Dining table "+str(x),x,7.5,3.5,1.2,"#997d5dff")
    for dy in (-1,1):
        furnishing(f"Dining bench {x}-{dy}",x,7.5+dy,3.1,.35,"#786949ff")
snack=action("Snack",22.5,4.5,.5,.45,"#efc36aff","E  Take snack for Tess","snack",hide=True)
label("Snack label","SNACK [E]",20.5,5.1,1.5,"#ffdb8fff")
# Laundry has washer drums, baskets, and Tess's fuse trade.
for x in (27,29.4,31.8,34.2):
    furnishing("Washer "+str(x),x,3.8,1.7,1.7,"#809896ff")
    rect("Washer drum "+str(x),x,3.8,1,1,"#304854ff",5)
    rect("Washer glass "+str(x),x,3.8,.6,.6,"#79a0a0ff",6)
furnishing("Laundry folding table",28,7.5,3,1.25,"#8a9b8fff")
for i in range(3):
    rect("Folded cloth "+str(i),27.2+i*.8,7.35,.6,.6,"#cad1b5ff",5)
tess_graph=trade("tess", "Tess / laundry", "snack", "fuse",
    "I've got a spare fuse. Bring me the snack from the canteen and it's yours.",
    "The fuse is yours. Rex has the tool you need. Rebuild a relay at the workshop bench.")
npc("Tess",33.4,7.3,orange,sequence=tess_graph)
label("Tess label","TESS [E]\nTRADE FOR FUSE",31.9,8.5,1.45,"#ffdb8fff")
# Security desk, monitors, and the actual access button.
furnishing("Security desk",41.4,3.5,5.8,1.1,"#778984ff")
for x in (39.5,41.2,42.9):
    rect("Monitor bezel "+str(x),x,3.3,1.2,.7,"#172c38ff",5)
    rect("Monitor screen "+str(x),x,3.3,.95,.43,"#72a692ff",6)
security_graph=graph("security",{
    "start":{"branch":[{"if":{"var":"code"},"next":"access"},{"next":"locked"}]},
    "access":{"speaker":"Security terminal","text":"Code accepted. Yard gate unlocked. Take your rebuilt relay to the maintenance breaker.","actions":[setvar("yard_access")]},
    "locked":{"speaker":"Security terminal","text":"Access code required. Talk to Milo in Cell Block A."}})
action("Security button",41.5,5.3,.7,.65,"#75c0c6ff","E  Enter Milo's code",extra=[c("Dialogue",sequence=security_graph,charactersPerSecond=0)])
label("Security button label","CODE BUTTON [E]\nUNLOCKS YARD",38.5,6.7,1.4,"#9ee2d7ff")
furnishing("Security filing cabinet",44.4,8.6,1,1.3,"#607d85ff")
for y in (3.2,5.2,8):
    furnishing("Utility crate "+str(y),49,y,2,1.5,"#827453ff")
# Workshop: inmate asks for the infirmary bandage, gives a screwdriver.
for x in (14.5,22.8):
    furnishing("Workshop tool rack "+str(x),x,20.5,1.5,2.4,"#736a52ff")
    for dy in (-.7,0,.7):
        rect(f"Rack tools {x}-{dy}",x,20.5+dy,1.1,.12,"#c7b589ff",5)
furnishing("Relay bench",18.5,25,6.3,1.3,"#9b825cff")
bench_graph=graph("relay",{
    "start":{"branch":[{"if":{"var":"relay"},"next":"done"},
        {"if":{"all":[{"var":"fuse"},{"var":"tool"}]},"next":"build"},{"next":"missing"}]},
    "build":{"speaker":"Relay workbench","text":"Fuse fitted. Contacts tightened. Rebuilt relay acquired! Carry it to the breaker in maintenance.","actions":[setvar("relay")]},
    "missing":{"speaker":"Relay workbench","text":"You need a fuse from Tess (trade a snack) and a screwdriver from Rex (trade a bandage)."},
    "done":{"speaker":"Relay workbench","text":"Relay ready. Use Milo's code at security and fit the relay at the maintenance breaker."}})
action("Relay workbench",18.5,23.9,.85,.55,"#d9b963ff","E  Build relay - fuse + tool",extra=[c("Dialogue",sequence=bench_graph,charactersPerSecond=0)])
label("Bench label","RELAY BENCH [E]",16,26.3,1.5,"#ffdb8fff")
rex_graph=trade("rex","Rex / workshop","bandage","tool",
    "Cut my hand on the bench. Bring a bandage from the infirmary and I'll lend you my screwdriver.",
    "Take my screwdriver. Combine it with Tess's fuse at the relay bench.")
npc("Rex",19,20.4,orange,sequence=rex_graph)
label("Rex label","REX [E] / BANDAGE > TOOL",15,21.8,1.4,"#ffdb8fff")
# Infirmary: beds, supplies, and a medic giving useful guidance.
for x in (28,34):
    furnishing("Hospital bed "+str(x),x,21.7,2,3,"#97b4a4ff")
    rect("Hospital pillow "+str(x),x,20.8,1.5,.55,"#dce2c5ff",5)
furnishing("Medical cabinet",34.5,26,2.2,1,"#a5b8a3ff")
rect("Cabinet cross horizontal",34.5,26,.6,.18,"#ad6452ff",5)
rect("Cabinet cross vertical",34.5,26,.18,.6,"#ad6452ff",5)
action("Bandage",33.2,25.2,.5,.4,"#f0e5baff","E  Take bandage for Rex","bandage",hide=True)
label("Bandage label","BANDAGE [E]",31.3,27,1.4,"#ffdb8fff")
npc("Doc",28.5,25.3,medic,"Supplies are by the cabinet. Rex needs a bandage. He's in the workshop, through the hall.")
label("Doc label","DOC [E]",27,26.7,1.4,"#b3dfcdff")
# Exercise yard: court lines, exercise benches, landscaping and a side conversation.
rect("Court surface",41.5,22.9,6.3,7.0,"#596850ff",-21)
for x in (38.5,44.5):
    rect("Court sideline "+str(x),x,23,.06,6.3,"#bbc59caa",-20)
for y in (19.85,23,26.15):
    rect("Court crossline "+str(y),41.5,y,6,.06,"#bbc59caa",-20)
rect("Hoop post",41.5,26.6,.2,.8,"#33413aff",4)
rect("Backboard",41.5,26.3,1.3,.18,"#c5c9a8ff",5)
for y in (15,17.2):
    furnishing("Yard bench "+str(y),40,y,2,.65,"#8d8d66ff")
npc("Ash",43.5,18.1,orange,"Maintenance is through the east door. Fit the relay in the breaker, then hit the green release button south of it. The last gate opens with E.")
label("Ash label","ASH [E] / EXIT TIP",38.2,18.8,1.2,"#ffdb8fff")
# Breaker and exit control deliberately sit inside the locked yard perimeter.
furnishing("Breaker cabinet",49.4,14,1.2,1.4,"#728e83ff")
action("Maintenance breaker",49.4,15.2,.7,.65,"#7cc5c2ff","E  Fit rebuilt relay","power",required="relay")
label("Breaker label","BREAKER [E]\nFIT RELAY",47,16.4,1.3,"#9ee2d7ff")
for x in (47.5,49,50.5):
    rect("Cable run "+str(x),x,19.3,.12,1.7,"#bea364ff",4)
action("Exit release button",47.4,23.5,.55,.55,"#8bdfadff","E  Release exit gate","exit_ready",required="power")
label("Release label","BUTTON [E]\nNEEDS POWER",48.3,23.0,1.0,"#baf4c6ff")
exit_goal=add("Blackwater escape zone",49,26.6,sprite(2.6,1.35,"#76c99499",2),
              box(2.4,1.15,"Sensor",True),c("Goal",requiredTag="player",enterOnComplete=True,
                exitDuration=.65,satisfiedColor="#c1ffc6ff"))
label("Exit label","FREEDOM",47.8,26.4,1.65,"#d3ffd6ff")
gate("Yard gate",37,14,False,"yard_access","E  Yard gate - security code required")
label("Yard gate label","GATE [E] / NEEDS CODE",37.8,13.5,1.0,"#ffdb8fff")
gate("Exit gate",49,24.8,True,"exit_ready","E  Exit gate - press powered release")
# Barrier across the whole lane means the button cannot be bypassed around the gate.
horizontal("Exit fence",24.8,46,52,49)
# An inmate guide gives the access code; the guard patrol is conversational, no stealth rules.
npc("Milo",8.6,15,orange,
    "The yard code is 0417. I've marked it in your progress log. Trade a canteen snack with Tess for a fuse, and an infirmary bandage with Rex for a tool. Build a relay in the workshop.",flag="code")
p1=add("Guard patrol west",14,14)
p2=add("Guard patrol east",34,14)
npc("Officer Vale",24,14,blue,
    "Association is open. Cells, canteen, laundry and infirmary are accessible. Yard access is controlled by the security terminal.",waypoints=[p1,p2])
label("Guard label","VALE / PATROLLING GUARD [E]",22,14.9,1.2,"#afcde0ff")
add("Overview camera",26,14,c("Camera",primary=True,mode="Fixed",orthographicHeight=36,targets=[player],clampToBounds=False))
add("Blackwater escape rules",0,0,c("LevelFlow",goals=[exit_goal],introDuration=1.1,
    introMessage="BLACKWATER / LOCKDOWN",completeDelay=999,completeMessage="ESCAPED FROM BLACKWATER!",
    restartSet="Global",restartAction="Restart",lockInputOnComplete=True))
# Real runtime HUD: room annotations above remain part of the playable scene.
panel("Title strip",0,0,1600,96,"#10232fff")
text("Level title","BLACKWATER / LOCKDOWN",45,22,3.1,"#f4d58dff")
text("Level description","A PRISON ESCAPE / TRADE WITH INMATES, REBUILD THE EXIT CONTROLS",46,59,1.55,"#c0d2c5ff")
text("Top controls","WASD MOVE   E TALK / USE\nARROWS SELECT   E CONFIRM   R RESTART",1115,27,1.55,"#e3e8cdff")
panel("Progress strip",0,910,1600,90,"#10232fff")
text("Inventory progress","CODE {code:0}   SNACK {snack:0}   BANDAGE {bandage:0}   FUSE {fuse:0}   TOOL {tool:0}",45,930,1.7,"#eed29aff")
text("Escape progress","RELAY {relay:0}  >  YARD ACCESS {yard_access:0}  >  POWER {power:0}  >  RELEASE {exit_ready:0}  >  EXIT",45,960,1.65,"#addac4ff")
text("Current interaction","{interaction_prompt}",1040,937,1.8,"#fbe2a2ff")
add("Result message",0,0,c("UiText",text="{level_message}",anchor="Center",offset=[0,0],scale=3,color="#fff0baff",layer=40))
scene={"format":"yk.scene","version":1,"settings":{"name":"Blackwater - Lockdown","gravity":[0,0],"background":"#182d38ff"},"entities":ENTITIES}
(ROOT / "scenes/blackwater.ykscene").write_text(json.dumps(scene,indent=2)+"\n")
print(f"Blackwater: {len(ENTITIES)} entities, player {player}, goal {exit_goal}")
