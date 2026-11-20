# SPDX-License-Identifier: MIT OR Apache-2.0
"""Pure transform hypotheses and pose metrics; no engine convention assumed."""
import math
import struct


def f32(x):
    try:return struct.unpack('<f',struct.pack('<f',x))[0]
    except OverflowError:return math.copysign(float('inf'),x)


def bits(x):
    return struct.unpack('<I',struct.pack('<f',f32(x)))[0]


def ulp(a,b):
    if not math.isfinite(a)or not math.isfinite(b):return None
    def ordered(x):
        n=bits(x)
        return 0x80000000-(n&0x7fffffff)if n&0x80000000 else 0x80000000+n
    return abs(ordered(a)-ordered(b))


def identity():
    return dict(rotation=[[float(i==j)for j in range(3)]for i in range(3)],translation=[0.]*3,scale=1.)


def transpose(m):return [list(r)for r in zip(*m)]


def compose(parent,local,hypothesis,arithmetic='double'):
    if arithmetic not in('double','f32_before','f32_after'):raise ValueError('unknown arithmetic')
    rounder=(lambda x:x)if arithmetic=='double'else f32
    def mul(a,b):return rounder(a*b)
    def add(a,b):return rounder(a+b)
    def dot(a,b):
        value=mul(a[0],b[0])
        for i in range(1,len(a)):value=add(value,mul(a[i],b[i]))
        return value
    pr,lr=parent['rotation'],local['rotation']
    a,b=(pr,lr)if hypothesis['rotation_product']=='parent_local'else(lr,pr)
    rotation=[[dot(a[i],[b[k][j]for k in range(3)])for j in range(3)]for i in range(3)]
    matrix=hypothesis['translation_matrix']
    m=pr if matrix.startswith('parent')else lr if matrix.startswith('local')else identity()['rotation']
    if matrix.endswith('transpose'):m=transpose(m)
    scales=dict(parent=parent['scale'],local=local['scale'],product=mul(parent['scale'],local['scale']),none=1.)
    factor=scales[hypothesis['translation_scale']]
    vector=local['translation']
    if arithmetic=='f32_before':
        vector=[mul(factor,v)for v in vector];offset=[dot(row,vector)for row in m]
    else:offset=[mul(factor,dot(row,vector))for row in m]
    translation=[add(parent['translation'][i],offset[i])for i in range(3)]
    world_scales=dict(product=mul(parent['scale'],local['scale']),parent=parent['scale'],local=local['scale'],one=1.)
    return dict(rotation=rotation,translation=translation,scale=world_scales[hypothesis['world_scale']])


def components(transform):
    return [v for row in transform['rotation']for v in row]+transform['translation']+[transform['scale']]


def quaternion(matrix,tolerance=1e-3):
    if any(not math.isfinite(v)for row in matrix for v in row):raise ValueError('nonfinite rotation')
    # The metric uses normalized quaternion projection only for near-SO(3)
    # matrices; a malformed, reflected or scaled matrix has no reported angle.
    residual=max(abs(sum(matrix[k][i]*matrix[k][j]for k in range(3))-(i==j))for i in range(3)for j in range(3))
    a=matrix;det=(a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]))
    if residual>tolerance or abs(det-1)>tolerance:raise ValueError('rotation is not near SO(3)')
    trace=sum(a[i][i]for i in range(3))
    if trace>0:
        s=2*math.sqrt(max(0,1+trace));q=[s/4,(a[2][1]-a[1][2])/s,(a[0][2]-a[2][0])/s,(a[1][0]-a[0][1])/s]
    else:
        i=max(range(3),key=lambda k:a[k][k]);j=(i+1)%3;k=(i+2)%3;s=2*math.sqrt(max(0,1+a[i][i]-a[j][j]-a[k][k]));q=[0.]*4
        if not s:raise ValueError('degenerate quaternion')
        q[0]=(a[k][j]-a[j][k])/s;q[i+1]=s/4;q[j+1]=(a[j][i]+a[i][j])/s;q[k+1]=(a[k][i]+a[i][k])/s
    norm=math.sqrt(sum(v*v for v in q));return [v/norm for v in q]


def rotation_angle(a,b):
    qa,qb=quaternion(a),quaternion(b)
    if sum(x*y for x,y in zip(qa,qb))<0:qb=[-x for x in qb]
    minus=math.sqrt(sum((x-y)**2 for x,y in zip(qa,qb)));plus=math.sqrt(sum((x+y)**2 for x,y in zip(qa,qb)))
    return 4*math.atan2(minus,plus)
