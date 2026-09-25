import sys; sys.path.insert(0,'.')
from emu import *
KEYS=('score','perf','great','good','bad','miss','combo','misscombo','life','judg')
NAMES={1:'PERFECT',2:'GREAT',3:'GOOD',4:'BAD',5:'MISS'}
def show(title, **kw):
    log, fin = run_hold(**kw)
    print("\n=== %s ===" % title)
    for t,k,ev,col0,hold in log:
        j = ev.get('judg',(0,0))[1]
        d = {a:b[1] for a,b in ev.items() if a in ('perf','great','good','bad','miss','combo','misscombo')}
        if j or d: print("t=%3d %s  notas=%s hold=%d  julgamento=%s  %s" % (t,k,[hex(x)[2:] for x in col0],hold,NAMES.get(j,'-'),d))
    print("final:", {k:fin[k] for k in ('perf','great','good','bad','miss','combo','life')}, " notas restantes:", [hex(x)[2:] for x in run_hold.__globals__.get('last',[])])
show("cedo: aperta em t=-6 e segura", press_t=-6, release_t=999)
show("tarde: aperta em t=+10 e segura", press_t=10, release_t=999)
show("solta no meio: aperta t=0, solta t=25", press_t=0, release_t=25)
show("nunca aperta", press_t=9999, release_t=9999)
show("aperta t=0, solta t=25, reaperta t=40", press_t=0, release_t=25, extra_press=None)
