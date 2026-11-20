# SPDX-License-Identifier: MIT OR Apache-2.0
"""Pure prospective projection families; camera semantics are empirical."""
import itertools
import math


def hypotheses():
    out=[]
    for order in ('rows','columns'):
        for perm in itertools.permutations(range(3)):
            for signs in itertools.product((-1,1),repeat=3):
                for near in ('slopes','near_plane'):
                    out.append(dict(id=f'{order}:{perm}:{signs}:{near}',family='pose_frustum',order=order,axes=list(perm),signs=list(signs),frustum=near))
    for order in ('rows','columns'):
        for y in (-1,1):
            out.append(dict(id=f'raw4x4:{order}:y{y}',family='raw4x4',order=order,y_sign=y))
    return out


def project(point,camera,h,width,height):
    """Returns client pixels with origin upper left; no fitted parameters."""
    if not all(math.isfinite(x)for x in point):raise ValueError('nonfinite point')
    port=camera['viewport'];left,right,top,bottom=port
    if h['family']=='raw4x4':
        m=camera['matrix'];v=list(point)+[1]
        clip=[sum((m[i][j]if h['order']=='rows'else m[j][i])*v[j]for j in range(4))for i in range(4)]
        if abs(clip[3])<1e-12:raise ValueError('zero homogeneous depth')
        x=(clip[0]/clip[3]+1)/2;y=(h['y_sign']*clip[1]/clip[3]+1)/2
    else:
        t=camera['world'];r=t['rotation'];delta=[point[i]-t['translation'][i]for i in range(3)]
        local=[sum((r[i][j]if h['order']=='rows'else r[j][i])*delta[j]for j in range(3))/t['scale']for i in range(3)]
        x,y,z=[local[k]*s for k,s in zip(h['axes'],h['signs'])]
        if z<=0:raise ValueError('behind camera')
        l,rr,tt,b,n,_=camera['frustum']
        factor=n if h['frustum']=='near_plane'else 1
        x=(factor*x/z-l)/(rr-l);y=(factor*y/z-b)/(tt-b)
    return [(left+x*(right-left))*width,(1-(bottom+y*(top-bottom)))*height]
