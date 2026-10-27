#!/usr/bin/env python3
"""Rebuild the castle's editable title, chapter selector and controls screens."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INK = '#101d2bff'
PANEL = '#20364bff'
GOLD = '#f5cc7dff'
TEXT = '#e7edf3ff'
MUTED = '#adc4d8ff'
MENU = 'scenes/menu.ykscene'
LEVELS = 'scenes/levels.ykscene'
CONTROLS = 'scenes/controls.ykscene'
HUD_DEFAULTS = dict(title='INVISIBLE CASTLE', completionTitle='THE OUTER HALL IS BEHIND YOU',
    completionText='The drainage passage leads deeper into the keep.',
    statusText='DAY 1 / EVENING FREE TIME', alertText='GUARD IS WATCHING',
    controlsText='WASD / ARROWS MOVE   E USE   ESC / P PAUSE   R RETRY   M MENU',
    continueText='ENTER - INNER KEEP   R - RETRY   M - MENU', restartText='R - RETRY   M - MENU')


def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + '\n')


class Screen:
    def __init__(self, name):
        self.name = name
        self.entities = []
        self.add('Pause policy', PauseMenu=dict(allowPause=False))
        self.add('Castle backdrop', UiImage=dict(texture='assets/ui/keep-preview.png',
            fillScreen=True, color='#8095aaff', layer=-10))
        self.add('Backdrop shade', UiImage=dict(fillScreen=True, color='#07111de8', layer=-9))
        self.panel('Top rule', 0, -304, 1080, 2, '#48617aff')
        self.panel('Bottom rule', 0, 292, 1080, 2, '#48617aff')

    def add(self, name, **components):
        eid = hashlib.sha1((self.name + '/' + name).encode()).hexdigest()[:16]
        self.entities.append(dict(id=eid, name=name,
            transform=dict(position=[0, 0], rotation=0, scale=[1, 1]),
            components=[dict(type=t, properties=p) for t, p in components.items()]))

    def panel(self, name, x, y, w, h, color=PANEL, layer=0):
        self.add(name, UiPanel=dict(anchor='Center', offset=[x, y], size=[w, h], color=color, layer=layer))

    def text(self, name, text, x, y, scale=2, color=TEXT):
        self.add(name, UiText=dict(text=text, anchor='Center', offset=[x, y], scale=scale, color=color, layer=8))

    def image(self, name, path, x, y, w, h):
        self.add(name, UiImage=dict(texture=path, anchor='Center', offset=[x, y], size=[w, h], layer=4))

    def button(self, name, title, x, y, w, h, destination):
        self.add(name, UiPanel=dict(anchor='Center', offset=[x, y], size=[w, h], color=PANEL, layer=1),
            UiButton=dict(changeScene=destination, hoverColor='#466077ff', pressedColor='#5a748bff'))
        if title:
            self.text(name + ' label', title, x, y, 2.5, GOLD)

    def shortcut(self, action, destination):
        self.add('Shortcut ' + action, EventAction=dict(inputAction=action,
            waitForRelease=True, once=True, changeScene=destination))

    def write(self, path):
        save(ROOT / path, dict(format='yk.scene', version=1,
            settings=dict(name=self.name, gravity=[0, 0], background=INK), entities=self.entities))


def decorate_level(scene, number):
    """Also used by chapter authors so regenerating either chapter retains the game flow."""
    scene['entities'] = [e for e in scene['entities'] if e['name'] not in
        ('Game pause menu', 'Return to main menu', 'Choose a chapter')]
    for name, kind, properties in (
        ('Game pause menu', 'PauseMenu', dict(menuScene=MENU, levelSelectScene=LEVELS)),
        ('Return to main menu', 'EventAction', dict(inputAction='Menu', waitForRelease=True, changeScene=MENU)),
        ('Choose a chapter', 'EventAction', dict(inputAction='LevelSelect', waitForRelease=True, changeScene=LEVELS))):
        scene['entities'].append(dict(id=hashlib.sha1(('frontend/' + name).encode()).hexdigest()[:16], name=name,
            transform=dict(position=[0, 0], rotation=0, scale=[1, 1]), components=[dict(type=kind, properties=properties)]))
    for entity in scene['entities']:
        for component in entity['components']:
            properties = component['properties']
            if component['type'] == 'InventoryHud':
                properties.update(HUD_DEFAULTS)
                if number == 2:
                    properties.update(title='INVISIBLE CASTLE / LEVEL 2',
                        completionTitle='THE INNER KEEP IS BEHIND YOU',
                        completionText='The green gate opens. Your friends follow you out.',
                        continueText='ENTER - MAIN MENU   R - RETRY   M - MENU')
            if component['type'] == 'LevelFlow':
                properties.update(nextScene='scenes/level2.ykscene' if number == 1 else MENU,
                    continueAction='Continue', completeDelay=30, waitForContinue=True)


def build_frontend():
    ui = ROOT / 'assets/ui'
    ui.mkdir(parents=True, exist_ok=True)
    title = Screen('Invisible Castle - Main Menu')
    title.text('Title', 'INVISIBLE\nCASTLE', -280, -177, 5.5, GOLD)
    title.text('Premise', 'SEARCH. CRAFT. OUTWIT THE GUARDS.', -280, -65, 1.5, MUTED)
    title.button('Play Button', 'PLAY  >', -280, 25, 430, 68, LEVELS)
    title.button('Controls Button', 'HOW TO PLAY', -280, 105, 430, 56, CONTROLS)
    title.text('Play hint', 'CLICK PLAY / ENTER', -280, 166, 1.5, MUTED)
    title.image('Castle view', 'assets/ui/keep-preview.png', 278, -62, 500, 281.25)
    title.text('Chapter count', 'TWO CHAPTERS. ONE WAY TO FREEDOM.', 278, 112, 1.5, GOLD)
    title.text('Chapter names', 'THE OUTER HALL / THE INNER KEEP', 278, 144, 1.5, MUTED)
    title.text('Controls', 'WASD / ARROWS MOVE    E INTERACT    ESC / P PAUSE', 0, 248, 1.5, MUTED)
    title.text('Footer', 'A CASTLE ESCAPE ADVENTURE', 0, 322, 1.5, MUTED)
    title.shortcut('Continue', LEVELS)
    title.shortcut('Help', CONTROLS)
    title.write(MENU)

    levels = Screen('Invisible Castle - Choose a Chapter')
    levels.text('Title', 'CHOOSE YOUR ESCAPE', 0, -252, 4, GOLD)
    levels.text('Intro', 'START SMALL IN THE HALL, OR TAKE ON THE WHOLE KEEP.', 0, -204, 1.5, MUTED)
    for number, x, name, detail, image, destination in (
        (1, -258, 'THE OUTER HALL', 'A SMALLER ESCAPE / TOOLS, KEYS AND ROPE', 'hall-preview', 'scenes/courtyard.ykscene'),
        (2, 258, 'THE INNER KEEP', 'THE FULL CASTLE / PATROLS AND POWER', 'keep-preview', 'scenes/level2.ykscene')):
        levels.button(f'Level {number} Card', '', x, 24, 492, 376, destination)
        levels.image(f'Level {number} preview', f'assets/ui/{image}.png', x, -27, 460, 258.75)
        levels.text(f'Level {number} title', f'{number}  {name}', x, 122, 2.5, GOLD)
        levels.text(f'Level {number} detail', detail, x, 156, 1.25, MUTED)
        levels.text(f'Level {number} play', f'PLAY LEVEL {number}  >', x, 192, 2, GOLD)
        levels.shortcut(f'Level{number}', destination)
    levels.button('Back Button', '<  MAIN MENU', 0, 255, 260, 44, MENU)
    levels.shortcut('Continue', 'scenes/courtyard.ykscene')
    levels.shortcut('Cancel', MENU)
    levels.shortcut('Menu', MENU)
    levels.text('Footer', 'CLICK A CHAPTER / PRESS 1 OR 2    ENTER STARTS LEVEL 1', 0, 322, 1.5, MUTED)
    levels.write(LEVELS)

    controls = Screen('Invisible Castle - How to Play')
    controls.text('Title', 'HOW TO ESCAPE', 0, -252, 4, GOLD)
    for i, (key, description) in enumerate([
        ('WASD / ARROWS', 'Move around the keep'), ('E / SPACE', 'Talk, search, craft and use gates'),
        ('ESC', 'Close a conversation or cancel a search'), ('ESC / P', 'Pause while exploring'),
        ('R', 'Restart the current chapter'), ('M / TAB', 'Main menu / chapter select')]):
        y = -163 + i * 43
        controls.text('Key ' + str(i), key, -310, y, 2, GOLD)
        controls.text('Action ' + str(i), description, 130, y, 1.5)
    controls.text('Goal', 'FOLLOW YOUR OBJECTIVE. TALK TO MARA FOR THE ESCAPE ROUTE.', 0, 132, 1.5, MUTED)
    controls.text('Advice', 'AVOID THE GUARD. HIDDEN SUPPLIES STAY WITH YOU IF CAUGHT.', 0, 164, 1.5, MUTED)
    controls.text('Controller', 'PAD: D-PAD MOVE / WEST USE / START PAUSE / EAST CLOSE', 0, 200, 1.5, MUTED)
    controls.button('Back Button', '<  MAIN MENU', 0, 255, 260, 44, MENU)
    controls.shortcut('Continue', MENU)
    controls.shortcut('Cancel', MENU)
    controls.shortcut('Menu', MENU)
    controls.text('Footer', 'PAUSING STOPS GUARDS, SEARCHES AND THE WORLD UNTIL YOU RESUME.', 0, 322, 1.5, MUTED)
    controls.write(CONTROLS)

    for number, path in [(1, 'scenes/courtyard.ykscene'), (2, 'scenes/level2.ykscene')]:
        scene = json.loads((ROOT / path).read_text())
        decorate_level(scene, number)
        save(ROOT / path, scene)
    project = json.loads((ROOT / 'project.ykproj').read_text())
    project['startScene'] = MENU
    project['window']['title'] = 'Invisible Castle'
    actions = next(s for s in project['input']['sets'] if s['name'] == 'Global')['actions']
    bindings = dict(Menu=['Key:M', 'Pad:Back'], LevelSelect=['Key:Tab'], Help=['Key:H'],
        Continue=['Key:Enter', 'Pad:South'],
        Level1=['Key:Num1', 'Pad:South'], Level2=['Key:Num2', 'Pad:North'],
        PauseUp=['Key:Up', 'Key:W', 'Pad:DPadUp'], PauseDown=['Key:Down', 'Key:S', 'Pad:DPadDown'])
    actions[:] = [a for a in actions if a['name'] not in bindings]
    actions.extend(dict(name=name, bindings=keys) for name, keys in bindings.items())
    for action in actions:
        if action['name'] == 'Restart':
            action['bindings'] = ['Key:R']
    save(ROOT / 'project.ykproj', project)
    print('Built title, chapter selection, controls and both chapter game flows.')


if __name__ == '__main__':
    build_frontend()
