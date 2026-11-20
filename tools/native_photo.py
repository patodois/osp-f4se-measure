# SPDX-License-Identifier: MIT OR Apache-2.0
"""Fixed nearest-neighbour extraction; never align, filter or fit exposure."""
from pathlib import Path
import hashlib,json
import numpy as np
from PIL import Image

WIDTH,HEIGHT=1280,720

def corner_mask(width=WIDTH,height=HEIGHT,radius=32):
    if width<2*radius or height<2*radius or radius<0:raise ValueError('invalid corner mask')
    # Frozen conservative square exclusions, independent of image content.
    mask=np.ones((height,width),dtype=bool)
    for y in (0,height-radius):
        for x in (0,width-radius):mask[y:y+radius,x:x+radius]=False
    return mask

def extract_array(rgb,crop):
    x,y,w,h=crop
    if rgb.dtype!=np.uint8 or rgb.ndim!=3 or rgb.shape[2]!=3:raise ValueError('expected RGB8')
    if (w,h)!=(2*WIDTH,2*HEIGHT) or min(x,y)<0 or x+w>rgb.shape[1] or y+h>rgb.shape[0]:raise ValueError('expected physical 2560x1440 client crop')
    blocks=rgb[y:y+h,x:x+w].reshape(HEIGHT,2,WIDTH,2,3)
    native=blocks[:,0,:,0,:].copy()
    nonuniform=np.any(blocks!=native[:,None,:,None,:],axis=(1,3,4))
    mask=corner_mask()
    ys,xs=np.nonzero(nonuniform)
    return native,mask,dict(crop=list(crop),native_size=[WIDTH,HEIGHT],sampling='top-left of every physical 2x2 block',mask='fixed 32x32 native squares at all four corners',nonuniform_blocks_xy=np.stack([xs,ys],axis=1).tolist(),nonuniform_blocks=int(nonuniform.sum()),nonuniform_unmasked=int((nonuniform&mask).sum()),uniform_fraction=float((~nonuniform).mean()))

def extract(source,output,crop):
    native,mask,report=extract_array(np.asarray(Image.open(source).convert('RGB')),crop)
    out=Path(output);Image.fromarray(native).save(out);Image.fromarray(mask.astype(np.uint8)*255).save(out.with_suffix('.mask.png'))
    report.update(source=str(source),source_sha256=hashlib.sha256(Path(source).read_bytes()).hexdigest(),native_sha256=hashlib.sha256(native.tobytes()).hexdigest())
    out.with_suffix('.extraction.json').write_text(json.dumps(report,indent=2)+'\n');return report

def difference(a,b,mask=None):
    if a.shape!=b.shape or a.ndim!=3 or a.shape[2]!=3 or a.dtype!=np.uint8 or b.dtype!=np.uint8:raise ValueError('expected equal RGB8 arrays')
    if mask is None:mask=corner_mask(a.shape[1],a.shape[0])
    if mask.shape!=a.shape[:2] or mask.dtype!=bool or not mask.any():raise ValueError('invalid mask')
    delta=np.abs(a.astype(np.int16)-b.astype(np.int16));p=delta.max(axis=2)[mask]
    return dict(pixels=int(mask.sum()),equal_fraction=float((p==0).mean()),max=int(p.max()),mean_pixel_max=float(p.mean()),mean_component=float(delta[mask].mean()),fraction_pixels_gt_1=float((p>1).mean()),fraction_pixels_gt_4=float((p>4).mean()),units='encoded RGB8 levels',alignment='none',exposure_fit='none')

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('output');p.add_argument('--crop',nargs=4,type=int,required=True);a=p.parse_args();print(json.dumps(extract(a.source,a.output,a.crop)))
