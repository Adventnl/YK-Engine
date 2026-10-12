"""Package freshly generated atlases into engine textures; reference images are never read.

Source sheets remain intact under assets/keep/source. Cell extraction, alpha-preserving
nearest-neighbor sizing, and baseline registration are deterministic export steps.
"""
from pathlib import Path
from PIL import Image

NEAREST = Image.Resampling.NEAREST

def cell(sheet, col, row, columns, rows):
    return sheet.crop((round(col*sheet.width/columns), round(row*sheet.height/rows),
                       round((col+1)*sheet.width/columns), round((row+1)*sheet.height/rows)))

def visible(image, threshold=80):
    box = image.getchannel('A').point(lambda p: 255 if p >= threshold else 0).getbbox()
    if box is None:
        raise ValueError('Generated atlas contains an empty sprite cell')
    return image.crop(box)

def fit(image, size, margin=1):
    image = visible(image)
    scale = min((size[0]-2*margin)/image.width, (size[1]-2*margin)/image.height)
    image = image.resize((max(1,round(image.width*scale)),max(1,round(image.height*scale))), NEAREST)
    out = Image.new('RGBA', size)
    out.alpha_composite(image, ((size[0]-image.width)//2, size[1]-image.height-margin))
    return out

def build_art(art: Path):
    source = art/'source'
    environment = Image.open(source/'environment.png').convert('RGBA')
    props = [('bed',(32,48)),('cabinet',(32,48)),('chest',(32,48)),('desk',(64,48)),
             ('bench',(64,48)),('craft',(64,48)),('barrel',(32,48)),('shelf',(32,48)),
             ('laundry',(32,48)),('drain',(64,48)),('table',(64,48)),('gate',(32,64)),
             ('plant',(32,48)),('statue',(32,48)),('banner',(32,48)),('crates',(32,48))]
    for index,(name,size) in enumerate(props):
        fit(cell(environment,index%4,index//4,4,4),size).save(art/(name+'.png'))
    tiles = Image.open(source/'tiles.png').convert('RGBA')
    for index,name in enumerate(['floor','corridor_floor','office_floor','water','wall','wall_vertical','wall_south','threshold']):
        cell(tiles,index%4,index//4,4,2).resize((32,32),NEAREST).save(art/(name+'.png'))
    # Workshop and laundry share the reference's cool stone palette.
    cell(tiles,0,0,4,2).resize((32,32),NEAREST).save(art/'work_floor.png')
    for role in ['prisoner','mara','old_prisoner','guard']:
        source_sheet = Image.open(source/(role+'.png')).convert('RGBA')
        frames = [visible(cell(source_sheet,col,row,8,4)) for row in range(4) for col in range(8)]
        # One scale for the whole character, with fixed feet alignment across directions.
        scale = min(30/max(frame.width for frame in frames),36/max(frame.height for frame in frames))
        sheet = Image.new('RGBA',(256,160))
        for index,frame in enumerate(frames):
            frame = frame.resize((max(1,round(frame.width*scale)),max(1,round(frame.height*scale))),NEAREST)
            sheet.alpha_composite(frame,((index%8)*32+(32-frame.width)//2,(index//8)*40+38-frame.height))
        sheet.save(art/(role+'.png'))
    accents = Image.open(source/'accents.png').convert('RGBA')
    # The generated embellishment sheet uses extra width for the rug. Export explicit
    # complete-object regions rather than truncating artwork at an assumed cell edge.
    def accent_region(left, top, right, bottom):
        return accents.crop((round(left*accents.width),round(top*accents.height),
                             round(right*accents.width),round(bottom*accents.height)))
    fit(accent_region(0,0,.36,.5),(160,96)).save(art/'rug.png')
    flames = [fit(accent_region(*box),(32,48),1) for box in
              [(.38,0,.51,.5),(.56,0,.72,.5),(.81,0,.95,.5)]]
    torch = Image.new('RGBA',(128,48))
    for col,index in enumerate([0,1,2,0]):
        # Use the exact same mounting pixels in every frame; only flame pixels vary.
        frame = flames[0].copy()
        frame.paste((0,0,0,0),(0,0,32,23))
        frame.alpha_composite(flames[index].crop((0,0,32,23)),(0,0))
        torch.alpha_composite(frame,(col*32,0))
    torch.save(art/'torch.png')
    for col,name in enumerate(['screwdriver','rope','iron_hook','brass_key']):
        bounds = [(0,.5,.26,1),(.275,.5,.535,1),(.55,.5,.74,1),(.77,.5,1,1)][col]
        fit(accent_region(*bounds),(32,32),2).save(art/(name+'.png'))
    # Crafted item combines its two actual inventory ingredients.
    grapple = Image.new('RGBA',(32,32))
    rope = Image.open(art/'rope.png').resize((24,24),NEAREST)
    hook = Image.open(art/'iron_hook.png').resize((20,20),NEAREST)
    grapple.alpha_composite(rope,(0,8)); grapple.alpha_composite(hook,(12,0))
    grapple.save(art/'grappling_line.png')
