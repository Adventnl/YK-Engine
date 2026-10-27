#!/usr/bin/env python3
"""Author Invisible Castle: exact-grid pixel art, scene, item data and animation clips.
Uses the retained current artwork and shared castle layout helpers.
"""
import json
from pathlib import Path
from build_frontend import HUD_DEFAULTS, decorate_level
from castle_masonry import ELEVATION
from castle_pixel_art import PPU

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT/'assets/keep'
ART.mkdir(parents=True, exist_ok=True)

def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2)+'\n')

save(ART/'torch.ykanim',dict(format='yk.animation',version=2,texture='assets/keep/torch.png',columns=4,rows=1,clips=[dict(name='burn',frames=[0,1,0,2],fps=7)]))
# Scene: a single readable keep, complete perimeter and two-tile doorways.
def c(kind,**p):return dict(type=kind,properties=p)
entities=[]
def add(name,x,y,*parts,tags=None):
    eid=f'{len(entities)+1:016x}'
    e=dict(id=eid,name=name,transform=dict(position=[x,y],rotation=0,scale=[1,1]),components=list(parts))
    if tags:e['tags']=tags
    entities.append(e);return eid

def sprite(name,w,h,**p):return c('SpriteRenderer',texture='assets/keep/'+name+'.png',size=[w,h],layer=10,**p)
def solid(w,h,offset=(0,0)):return c('Collider',size=[w,h],offset=list(offset),layer='Solid',friction=0)
# Avoid ambiguous partial tile origins: all floor rectangles start at an integer corner.
def surface(name,x,y,w,h,art='floor'):
    add(name,x+w/2,y+h/2,c('SpriteRenderer',texture='assets/keep/'+art+'.png',size=[w,h],layer=-20,drawMode='Tiled',tileSize=[1,1]))
surface('Keep flagstones',0,0,32,18)
surface('Common corridor',1,8,19,3,'corridor_floor')
surface('Workshop floor',1,12,9,5,'work_floor');surface('Laundry floor',11,12,9,5,'work_floor')
surface('Guard office floor',13,1,7,6,'office_floor');surface('East tower floor',21,1,10,16,'office_floor')
surface('Drainage water',21,15,10,2,'water')
wall_cells=set()
for x in range(32):wall_cells.update([(x,0),(x,17)])
for y in range(18):wall_cells.update([(0,y),(31,y)])
for y in range(1,7):wall_cells.update([(6,y),(12,y)])
for x in range(1,20):
    if x not in (3,4,9,10,16,17):wall_cells.add((x,7))
    if x not in (7,8,15,16):wall_cells.add((x,11))
for y in range(12,17):wall_cells.add((10,y))
for y in range(1,17):
    if y not in (9,10):wall_cells.add((20,y))
for x in range(21,31):
    if x not in (25,26):wall_cells.add((x,7))
for x,y in sorted(wall_cells):
    vertical=(x in (0,6,12,20,31) or (x==10 and y>=12)) and y not in (0,7,11,17)
    art='wall_vertical' if vertical else ('wall_south' if y==17 else 'wall')
    add('Masonry %d %d'%(x,y),x+.5,y+.5,sprite(art,1,1,layerOverride=4),solid(1,1))
# The south coping projects into the side passage. Preserve that stone return
# and fit the gate to the remaining opening, level with the corridor wall.
gate_height = 2 - ELEVATION / PPU
# Flush gold doorway thresholds sit beneath actors, without changing collision.
for x,y,w,h in [(3,7,2,1),(9,7,2,1),(16,7,2,1),(7,11,2,1),(15,11,2,1),(25,7,2,1),(20,9,1,gate_height)]:
    add('Brass doorway threshold',x+w/2,y+h/2,c('SpriteRenderer',texture='assets/keep/threshold.png',size=[w,h],layer=-16,drawMode='Tiled',tileSize=[1,1]))
add('Common room heraldic rug',4,9.5,sprite('rug',5,2.5,layerOverride=-15))
add('Office heraldic rug',17,3.8,sprite('rug',4.5,3,layerOverride=-15))
# Correct layer overrides at the authoring boundary, keeping sprite helper simple.
for e in entities:
    for part in e['components']:
        p=part['properties']
        if 'layerOverride' in p:p['layer']=p.pop('layerOverride')
hero=add('Explorer',3.5,4.5,sprite('prisoner',1,1.25,offset=[0,-.375],ySort=True),c('AnimatedSprite',animation='assets/keep/prisoner.ykanim',clip='idle_down',flipParameter=''),c('TopDownController',speed=4,acceleration=60),c('PlayerInput',actionSet='Player1'),c('Interactor'),c('RigidBody',type='Dynamic',gravityScale=0,linearDamping=0,fixedRotation=True,bullet=True),c('Collider',size=[.5,.35],offset=[0,0],layer='Player',friction=0),c('Inventory',slots=6),c('Crafter'),tags=['player'])
add('Camera',3.5,4.5,c('Camera',mode='Follow',targets=[hero],orthographicHeight=12.083333,viewportInset=[0,140],smoothTime=.08,clampToBounds=True,boundsMin=[0,0],boundsMax=[32,18]))
# Static furnishings: the visible footprint and physical footprint agree.
def prop(name,art,x,y,w=1,h=1.5,parts=(),foot=.7):
    return add(name,x,y,sprite(art,w,h,offset=[0,-h/2+.25],ySort=True),solid(w*.82,foot,offset=(0,-foot/2+.25)),*parts)
def search(name,art,x,y,item,flag,text,required=(),locked='',w=1):
    return prop(name,art,x,y,w=w,parts=[c('Interactable',prompt='E - SEARCH '+name.upper(),range=1.8),c('Inventory',slots=3,startItems=[dict(item=item,count=1)]),c('Container',searchSeconds=2.2,hiddenSlots=3,openRequires={'type':'HasItem','item':required[0]} if required else None),c('SearchAction',title=name.upper(),duration=2.2,successFlag=flag,successText=text,requiredItems=list(required),lockedText=locked)])
prop('Your straw bed','bed',2,3.2);prop('Cellmate bed','bed',5,3.2)
prop('Stores shelf','shelf',8,2.4);prop('Stores barrel','barrel',11,2.6)
search('Locked cabinet','cabinet',9.5,4.7,'rope','rope_found','Found rope. Pair it with an iron hook at the workshop.',required=['screwdriver'],locked='LOCKED - pry the latch with a screwdriver.')
search('Tool bench','bench',3,14.3,'screwdriver','tool_found','Found a screwdriver. It can open the locked stores.',w=2)
prop('Workshop barrel','barrel',2,16);prop('Workshop shelf','shelf',2,12.9)
prop('Rope workbench','craft',7,14.3,w=2,parts=[c('CraftingStation',station='workbench',range=2),c('Interactable',prompt='E - CRAFT GRAPPLING LINE (ROPE + IRON HOOK)',range=1.8),c('SearchAction',title='MAKE A GRAPPLING LINE',verb='TYING THE ROPE TO THE HOOK',recipe='grappling_line',duration=2.5,successFlag='line_made',successText='Grappling line made. Reach the drain beyond the iron gate.',emptyText='Your grappling line is ready for the drainage passage.')])
search('Laundry chest','chest',14,14.5,'iron_hook','hook_found','Found an iron hook. Tie it to rope at the workshop.')
for x in (12,17,19):prop('Folded linen','laundry',x,13.5)
search('Warden desk','desk',17,3.2,'brass_key','key_found','Found the brass key. It opens the east tower gate.',w=2)
prop('Office shelves','shelf',14,2.4);prop('Office chest','chest',19,2.5)
prop('Common room table','table',4,9.6,w=2,h=1.5)
prop('East tower shelf','shelf',29,3);prop('East tower barrel','barrel',30,6)
prop('East tower chest','chest',25,3)
prop('East archive shelf','shelf',23,3)
prop('East archive desk','desk',27,5,w=2)
prop('Drain service barrel','barrel',23,13.5)
# Reference-style furnishing accents stay out of doorways and escape interactions.
for x,y in [(1.6,10.5),(19,10.5),(11,6),(30,10.8),(22,16.4)]:
    prop('Stores fern' if (x,y)==(11,6) else 'Terracotta fern','plant',x,y,foot=.45)
prop('Common room armor','statue',10.8,9.6,foot=.55)
prop('Tower armor','statue',30,3.5,foot=.55)
prop('Stores crates','crates',10.8,4.8)
prop('Workshop crates','crates',9,16)
for x,y in [(3.5,1),(9.5,1),(16.5,1),(25.5,1)]:
    add('Royal blue banner',x,y+.5,sprite('banner',.7,1.3,layerOverride=8))
for name,art,x,y,w in [('CELLS','cells',3.5,7.4,2),('LOCKED STORES','locked_stores',9.5,7.4,3),('GUARD OFFICE','guard_office',16.5,7.4,3),('WORKSHOP','workshop',5.5,11.4,2.5),('LAUNDRY','laundry',15,11.4,2.5),('DRAINAGE','drainage',27.5,12,2.5)]:
    add(name+' sign',x,y,sprite('sign_'+art,w,.5,layerOverride=7))
for e in entities:
    for part in e['components']:
        p=part['properties']
        if 'layerOverride' in p:p['layer']=p.pop('layerOverride')
for x,y in [(1.5,7),(6.5,7),(12.5,7),(19.5,7),(1.5,11),(10.5,11),(19.5,11),(31.5,12)]:
    add('Fixed wall torch',x,y-.5,sprite('torch',.75,1.125),c('AnimatedSprite',animation='assets/keep/torch.ykanim',clip='burn',flipParameter=''))
add('Mara',8,9.6,sprite('mara',1,1.25,offset=[0,-.375],ySort=True),c('AnimatedSprite',animation='assets/keep/mara.ykanim',clip='idle_left',flipParameter=''),solid(.5,.35),c('Interactable',prompt='E - TALK TO MARA',range=1.5,setFlag='met_mara'),c('Dialogue',speaker='Mara',pages=['The warden has sealed Invisible Castle. At dusk, the east drain leads out beneath the castle wall.', 'Find a screwdriver in the workshop. Pry open the stores for rope, and take an iron hook from the laundry. Tie them at the workbench.', 'The brass key is on the warden desk. Wait for the guard to walk away, then search it. Open the east gate and climb down the drain.'],charactersPerSecond=0))
add('Old prisoner',5.7,9.3,sprite('old_prisoner',1,1.25,offset=[0,-.375],ySort=True),c('AnimatedSprite',animation='assets/keep/old_prisoner.ykanim',clip='idle_right',flipParameter=''),solid(.5,.35),c('Interactable',prompt='E - TALK TO ELIAS',range=1.3),c('Dialogue',speaker='Elias',pages=['We were locked up for refusing the warden\'s taxes. Mara found a way out. The workshop door is south of the common room.', 'The guard watches the office. If he spots you, leave before the warning fills. He cannot search the supplies hidden in your clothes.'],charactersPerSecond=0))
points=[add('Guard patrol '+str(i),x,y) for i,(x,y) in enumerate([(14.5,5.4),(18.5,5.4),(17,5.4),(17,9.4),(12,9.4),(17,9.4),(17,5.4)])]
add('Keep guard',14.5,5.4,sprite('guard',1,1.25,offset=[0,-.375],ySort=True),c('AnimatedSprite',animation='assets/keep/guard.ykanim',clip='idle_down',flipParameter=''),c('NpcPath',waypoints=points,speed=1.65,waitSeconds=1.2),c('RigidBody',type='Kinematic',gravityScale=0,fixedRotation=True),c('Collider',size=[.5,.35],layer='Solid',friction=0),c('PatrolWatch', caughtText='Caught! Back to your cell. Your hidden supplies are safe.',actor=hero,restrictedMin=[13,1],restrictedMax=[20,7],returnPosition=[3.5,4.5],range=3.1,catchSeconds=2.8))
add('Castle gate',20.5,9+gate_height/2,sprite('gate',1,gate_height),solid(1,gate_height),c('StateGate',startsLocked=True,unlockFlag='key_found',persistFlag='gate_open'),c('Interactable',prompt='E - UNLOCK EAST GATE / BRASS KEY',range=1.8))
prop('Drain grate','drain',28.5,15,w=2,h=1.5,parts=[c('Interactable',prompt='E - OPEN DRAIN / SCREWDRIVER + GRAPPLING LINE',range=1.8),c('SearchAction',title='ESCAPE THROUGH THE DRAIN',verb='LOOSENING THE GRATE AND SECURING YOUR ROPE',requiredItems=['screwdriver','grappling_line'],lockedText='Bring a screwdriver and a crafted grappling line.',duration=3,successFlag='drain_open',successText='Grate opened. Step into the passage behind it.',emptyText='The passage is open. Walk down the rope to escape.')])
add('Escape passage',28.5,16.65,c('Collider',size=[1.5,.5],isTrigger=True,layer='Sensor'),c('Goal',requiredTag='player',requiredFlag='drain_open',enterOnComplete=True))
add('Escape story',0,0,c('ObjectiveList',flags=['met_mara','tool_found','rope_found','hook_found','line_made','key_found','gate_open','drain_open'],instructions=['Talk to Mara in the common room.','Search the tool bench in the south workshop.','Pry open the locked cabinet in the north stores.','Search the chest in the south laundry.','Make a grappling line at the workshop workbench.','Steal the brass key from the warden desk. Avoid the guard.','Unlock the iron gate to the east tower.','Unscrew the drain grate and secure your grappling line.'],completeText='Walk into the open drainage passage to escape.'),c('InventoryHud', **HUD_DEFAULTS,actor=hero),c('LevelFlow',goals=[entities[-1]['id']],introDuration=0,restartOnDeath=False,completeDelay=30,completeMessage='YOU ESCAPED INVISIBLE CASTLE',nextScene='scenes/level2.ykscene',continueAction='Continue'))
# Completed work retracts the grate, revealing the actual passage and clearing its collision.
for e in entities:
    if e['name']=='Drain grate':
        e['components'].append(c('StateGate',startsLocked=True,unlockFlag='drain_open',persistFlag='grate_open'))
scene=dict(format='yk.scene',version=1,settings=dict(name='Invisible Castle - A Castle Escape',gravity=[0,0],background='#16191fff'),entities=entities)
from castle_reference import style_level_one
style_level_one(scene)
decorate_level(scene, 1)
save(ROOT/'scenes/courtyard.ykscene',scene)
items=[dict(id=i,displayName=n,icon='assets/keep/'+i+'.png',tags=['escape']) for i,n in [('screwdriver','Screwdriver'),('rope','Rope'),('iron_hook','Iron hook'),('brass_key','Brass key'),('grappling_line','Grapple')]]
save(ROOT/'data/keep.ykdata',dict(format='yk.data',version=1,items=items,recipes=[dict(id='grappling_line',displayName='Grappling line',station='workbench',ingredients=[dict(item='rope'),dict(item='iron_hook')],output=dict(item='grappling_line'))]))
p=ROOT/'project.ykproj';project=json.loads(p.read_text());project['name']='Invisible Castle';project['startScene']='scenes/menu.ykscene';project['window']['title']='Invisible Castle';project['build']['productName']='Invisible Castle';project['textures']['pixelsPerUnit']=35
save(p,project)
print('Authored',len(entities),'entities and',len(list(ART.glob('*.png'))),'pixel art assets.')
