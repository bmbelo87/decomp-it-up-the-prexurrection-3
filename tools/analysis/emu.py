import pefile, struct
from unicorn import *
from unicorn.x86_const import *

pe = pefile.PE('pumpy_unpacked.exe'); BASE = pe.OPTIONAL_HEADER.ImageBase
ROWS = 0x50000000; STACK = 0x7ff00000; SENT = 0x60000000
P0 = 0xda22c8

def new_uc():
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    for s in pe.sections:
        va = BASE + s.VirtualAddress
        size = (max(s.Misc_VirtualSize, s.SizeOfRawData) + 0xfff) & ~0xfff
        mu.mem_map(va, size)
        mu.mem_write(va, s.get_data()[:min(len(s.get_data()), size)])
    mu.mem_map(ROWS, 0x100000); mu.mem_map(STACK, 0x100000); mu.mem_map(SENT, 0x1000)
    return mu

def w32(mu, a, v): mu.mem_write(a, struct.pack('<i', v) if v < 0 else struct.pack('<I', v))
def r32(mu, a): return struct.unpack('<i', bytes(mu.mem_read(a, 4)))[0]
def rb(mu, a, n=1): return bytes(mu.mem_read(a, n))

# janelas Easy, unidades ja truncadas p/ bpm=120 (11,...): usaremos os inteiros brutos abaixo
WIN_EASY = [-12, 7, -17, 12, -22, 17, -27, 22]   # (cedo/tarde no sentido do original: delta>lo && delta<hi)

def scenario(mode=0x10, rows=None, base=0, deltas=None, held=0, pressed=0, player=0, win=WIN_EASY,
             pre=None, score=0, life=500, speed=500, combo=0, chart=0):
    mu = new_uc()
    w32(mu, 0x442728, mode)
    # estrutura do jogador
    P = P0 + player * 0x98
    mu.mem_write(P, b'\0' * 0x98)
    w32(mu, P + 0x5c, life); w32(mu, P + 0x60, speed); w32(mu, P + 0x50, combo); w32(mu, P, score)
    w32(mu, P + 0x7c, chart); w32(mu, P + 0x80, 0)
    # janelas
    for i, v in enumerate(win): w32(mu, 0xda222c + 4 * i, v)
    w32(mu, 0xda24b8, base)
    for i in range(20): w32(mu, 0xda2400 + 4 * i, 0x7fffffff)
    for i, d in enumerate(deltas or []): w32(mu, 0xda2400 + 4 * i, d)
    # chart
    cs = 0xd39130 + chart * 0x88
    w32(mu, cs, len(rows)); w32(mu, cs + 4, ROWS)
    mu.mem_write(ROWS, b''.join(bytes(r) for r in rows))
    w32(mu, 0x8baffc, held); w32(mu, 0xc1a494, pressed)
    for i in range(10): mu.mem_write(0xda2298 + i, b'\0'); mu.mem_write(0xda2494 + i, b'\x18')
    if pre: pre(mu)
    # pilha e chamada
    sp = STACK + 0x80000
    mu.mem_write(sp, struct.pack('<II', SENT, player))
    mu.reg_write(UC_X86_REG_ESP, sp)
    mu.emu_start(0x40e330, SENT, count=2000000)
    out = dict(
        rows=[list(rb(mu, ROWS + 10 * i, 10)) for i in range(len(rows))],
        score=r32(mu, P), perf=r32(mu, P + 0x3c), great=r32(mu, P + 0x40), good=r32(mu, P + 0x44),
        bad=r32(mu, P + 0x48), miss=r32(mu, P + 0x4c), combo=r32(mu, P + 0x50), misscombo=r32(mu, P + 0x54),
        maxcombo=r32(mu, P + 0x58), life=r32(mu, P + 0x5c), speed=r32(mu, P + 0x60),
        pend=r32(mu, P + 0x64), judg=r32(mu, P + 0x68), flag74=r32(mu, P + 0x74),
        holdflag=list(rb(mu, 0xda2298, 10)), fx=list(rb(mu, 0xda2494, 10)),
        pressed=r32(mu, 0xc1a494), held=r32(mu, 0x8baffc))
    return out

if __name__ == '__main__':
    # tap no painel 1 (col 0 = DL, mascara 0x1), delta 0 => Perfect
    rows = [[0]*10 for _ in range(30)]
    rows[10][0] = 1          # nota tap col0 (DL)
    deltas = [(10 - i) * 40 for i in range(20)]   # delta[i] p/ linha base+i (positivo = futuro)
    base = 0
    o = scenario(rows=rows, base=base, deltas=[ (i-10)*(-40) for i in range(20)], pressed=0x1)
    print({k: v for k, v in o.items() if k != 'rows'}); print("row10:", o['rows'][10])


class Session:
    """Estado persistente entre frames: linhas, jogador, contadores."""
    def __init__(self, rows, mode=0x10, player=0, win=WIN_EASY, spacing=15, base=0, life=500, speed=500):
        self.mu = new_uc(); mu = self.mu
        self.rows, self.mode, self.player, self.spacing, self.base = rows, mode, player, spacing, base
        w32(mu, 0x442728, mode)
        P = self.P = P0 + player * 0x98
        mu.mem_write(P, b'\0' * 0x98)
        w32(mu, P + 0x5c, life); w32(mu, P + 0x60, speed)
        w32(mu, 0xda2260, 200); w32(mu, 0xda22c0, 1000); w32(mu, 0xda225c, -700)
        for i, v in enumerate(win): w32(mu, 0xda222c + 4 * i, v)
        cs = 0xd39130
        w32(mu, cs, len(rows)); w32(mu, cs + 4, ROWS)
        mu.mem_write(ROWS, b''.join(bytes(r) for r in rows))
        for i in range(10): mu.mem_write(0xda2298 + i, b'\0'); mu.mem_write(0xda2494 + i, b'\x18')
        self.prev = None

    def getp(self, off): return r32(self.mu, self.P + off)

    def snapshot(self):
        return dict(score=self.getp(0), perf=self.getp(0x3c), great=self.getp(0x40), good=self.getp(0x44),
                    bad=self.getp(0x48), miss=self.getp(0x4c), combo=self.getp(0x50), misscombo=self.getp(0x54),
                    life=self.getp(0x5c), speed=self.getp(0x60), pend=self.getp(0x64), judg=self.getp(0x68))

    def step(self, t, held=0, pressed=0):
        """t = posicao atual em unidades; delta_i = posLinha_i - t (positivo = cedo)."""
        mu = self.mu
        # base = linha atual - 10 (como o original), sem ficar negativa
        cur = max(0, int(round(t / self.spacing)) + 10)      # linha 'no receptor' (head em 10)
        base = max(0, cur - 10)
        w32(mu, 0xda24b8, base)
        for i in range(20):
            row = base + i
            d = self.spacing * (row - 10) - t if row < len(self.rows) else 0x7fffffff
            w32(mu, 0xda2400 + 4 * i, int(d))
        # [0xda24b4] = linha com |delta| < 5 (a 'linha no receptor'); mantem o ultimo valor se nao ha nenhuma
        for i in range(20):
            row = base + i
            if row < len(self.rows) and abs(self.spacing * (row - 10) - t) < 5: self.nearest = row
        w32(mu, 0xda24b4, getattr(self, 'nearest', 1) or 1)
        w32(mu, 0x8baffc, held); w32(mu, 0xc1a494, pressed)
        sp = STACK + 0x80000
        mu.mem_write(sp, struct.pack('<II', SENT, self.player))
        mu.reg_write(UC_X86_REG_ESP, sp)
        mu.emu_start(0x40e330, SENT, count=2000000)
        self.rows_now = [list(rb(mu, ROWS + 10 * i, 10)) for i in range(len(self.rows))]
        snap = self.snapshot()
        snap['hold'] = rb(mu, 0xda2298, 1)[0]
        return snap


def run_hold(press_t, release_t, hold_rows=(10, 14), verbose=True, mode=0x10, extra_press=None, until=80):
    rows = [[0]*10 for _ in range(40)]
    a, b = hold_rows
    rows[a][0] = 0x0a
    for r in range(a+1, b): rows[r][0] = 0x0b
    rows[b][0] = 0x0c
    s = Session(rows, mode=mode)
    prev = s.snapshot(); log = []
    down = False; t = -30
    while t <= until:
        pressed = 0; held = 0
        should_hold = press_t <= t < release_t
        if should_hold and not down: pressed = 1
        if extra_press and t in extra_press and not should_hold: pressed = 1
        down = should_hold
        if should_hold or (pressed): held = 1
        snap = s.step(t, held=held, pressed=pressed)
        ev = {k: (prev[k], snap[k]) for k in snap if k in prev and snap[k] != prev[k]}
        col0 = [r[0] for r in s.rows_now[a:b+1]]
        if ev or (verbose == 'all'):
            log.append((t, 'P' if pressed else ('H' if held else '-'), ev, col0, snap['hold']))
        prev = snap
        t += 3
    return log, prev

if __name__ == '__main__' and False:
    pass
