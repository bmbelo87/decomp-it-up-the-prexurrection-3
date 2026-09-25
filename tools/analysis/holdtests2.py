import sys; sys.path.insert(0,'.')
from emu import *
NAMES={1:'PERFECT',2:'GREAT',3:'GOOD',4:'BAD',5:'MISS'}
def go(title, mode, press_t, release_t, repress_t=None, until=110):
    rows=[[0]*10 for _ in range(40)]
    rows[10][0]=0x0a
    for r in (11,12,13): rows[r][0]=0x0b
    rows[14][0]=0x0c
    s=Session(rows,mode=mode); prev=s.snapshot(); ev=[]
    t=-30; down=False
    while t<=until:
        want = (press_t<=t<release_t) or (repress_t is not None and t>=repress_t)
        pressed = 1 if (want and not down) else 0
        down=want
        snap=s.step(t, held=1 if want else 0, pressed=pressed)
        if snap['judg'] and snap['judg']!=prev['judg']:
            pass
        d=[(k,snap[k]) for k in ('perf','great','good','bad','miss') if snap[k]!=prev[k]]
        if d: ev.append("t=%d:%s"%(t,'/'.join(k+str(v) for k,v in d)))
        prev=snap; t+=3
    print("%-52s %s | notas col0 = %s" % (title, ' '.join(ev), [hex(r[0])[2:] for r in s.rows_now[10:15]]))
for label,mode in (("1a metade (0x801 NM/HD/DV, P1)",0x801),):
    print("\n#### "+label)
    go("segura tudo", mode, 0, 999)
    go("solta 5 un ANTES da cauda (t=55)", mode, 0, 55)
    go("solta 12 un antes da cauda (t=48)", mode, 0, 48)
    go("solta em t=25, reaperta em t=40", mode, 0, 25, repress_t=40)
    go("solta em t=25, reaperta em t=55", mode, 0, 25, repress_t=55)
