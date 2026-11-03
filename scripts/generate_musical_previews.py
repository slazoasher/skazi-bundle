"""Render original musical website previews (not plugin output).
Deterministic 44.1 kHz stereo arrangements in A minor, with antialiased additive
voices, musical envelopes, percussion, tempo delays and peak-safe mastering.
Requires numpy; no downloaded audio or third-party samples.
"""
from pathlib import Path
import numpy as np
import wave, json
SR=44100; BPM=138; BEAT=60/BPM; LENGTH=16*BEAT+1.3; N=int(LENGTH*SR)
OUT=Path(__file__).resolve().parent.parent/'docs'/'audio'
rng=np.random.default_rng(20261026)
def hz(m): return 440*2**((m-69)/12)
def env(t,d,attack=.008,decay=.22,sustain=.25,release=.10):
    a=np.minimum(t/attack,1)
    e=sustain+(1-sustain)*np.exp(-np.maximum(t-attack,0)/decay)
    return a*e*np.minimum(np.maximum((d-t)/release,0),1)
def voice(note,d,kind='pluck',pan=0):
    t=np.arange(int(d*SR))/SR; f=hz(note); x=np.zeros(len(t))
    if kind=='sub':
        x=np.sin(2*np.pi*f*t)+.15*np.sin(4*np.pi*f*t)
        e=env(t,d,.006,.14,.55,.045)
    else:
        count={'pluck':9,'pad':14,'bass':10,'acid':18,'bell':4}[kind]
        for k in range(1,count+1):
            if f*k>SR*.42: break
            decay={'pluck':.25,'pad':3,'bass':.09,'acid':.16,'bell':.5}[kind]
            harmonic=np.exp(-t*k/(decay*4))/k**(1.5 if kind=='bell' else 1.25)
            if kind=='bell': phase=2*np.pi*f*(k if k<3 else k+.005)*t
            else: phase=2*np.pi*f*k*t
            x+=np.sin(phase)*harmonic
        e=env(t,d,.035 if kind=='pad' else .004,.65 if kind=='pad' else .15,.7 if kind=='pad' else .12,.3 if kind=='pad' else .06)
    x*=e
    return np.column_stack((x*np.sqrt((1-pan)/2),x*np.sqrt((1+pan)/2)))
def put(dst,x,start,amp=1):
    i=int(start*SR); n=min(len(x),len(dst)-i)
    if n>0 and i>=0: dst[i:i+n]+=x[:n]*amp

def drum(kind):
    d={'kick':.32,'hat':.07,'open':.19,'clap':.17}[kind];t=np.arange(int(d*SR))/SR
    if kind=='kick':
        phase=2*np.pi*(48*t+105*(1-np.exp(-t*48))/48)
        x=np.sin(phase)*np.exp(-t*12)+.07*rng.normal(size=len(t))*np.exp(-t*230)
        x*=np.minimum(t/.0015,1)
    else:
        n=rng.normal(size=len(t));n=np.r_[0,np.diff(n)]*.25
        if kind=='clap':
            bursts=sum(np.exp(-np.maximum(t-o,0)*80)*(t>=o) for o in [0,.012,.025])
            x=n*bursts+.12*np.sin(2*np.pi*185*t)*np.exp(-t*40)
        else: x=n*np.exp(-t*(75 if kind=='hat' else 23))*np.minimum(t/.001,1)
    x*=np.minimum((d-t)/.006,1)
    return np.column_stack((x,x))
DRUM={k:drum(k) for k in ['kick','hat','open','clap']}
def drums(dst,level=1):
    for b in range(16):
        put(dst,DRUM['kick'],b*BEAT,.62*level)
        put(dst,DRUM['open'],(b+.5)*BEAT,.13*level)
        if b%4 in [1,3]: put(dst,DRUM['clap'],b*BEAT,.23*level)
        for s in [0,.25,.75]:put(dst,DRUM['hat'],(b+s)*BEAT,.085*level)
def delay(src,wet=.22):
    out=src.copy()
    for j in range(1,5):
        shift=int(BEAT*.75*j*SR)
        out[shift:]+=src[:-shift,::-1 if j%2 else 1]*wet*(.54**(j-1))
    for ms,g in [(31,.04),(47,.035),(71,.03),(113,.025)]:
        shift=int(ms*SR/1000);out[shift:]+=src[:-shift,::-1]*g
    return out
ROOTS=[45,41,48,43] # Am, F, C, G
MELODY=[0,7,12,15,12,7,3,7,0,7,10,15,12,10,7,3]
def arrange(name):
    mix=np.zeros((N,2)); music=np.zeros_like(mix); bass=np.zeros_like(mix)
    drums(mix,1 if name=='kick' else .62)
    for bar,root in enumerate(ROOTS):
        for b in range(4):
            beat=bar*4+b
            steps=[.25,.5,.75] if name in ['psy','acid','drive','sub'] else [.5]
            for s in steps:
                kind='sub' if name in ['sub','echo','arp'] else 'bass'
                put(bass,voice(root,.16 if len(steps)>1 else .26,kind),(beat+s)*BEAT,.24 if kind=='sub' else .16)
        if name not in ['kick','sub','acid','psy']:
            intervals=[0,3,7] if bar==0 else [0,4,7]
            for interval in intervals:
                for det in [-.035,.035]:
                    put(music,voice(root+12+interval+det,BEAT*3.9,'pad',-.55 if det<0 else .55),bar*4*BEAT,.07)
        if name!='kick':
            for step in range(8):
                pitch=root+24+MELODY[(step+bar*2)%16]
                kind='acid' if name=='acid' else 'bell' if name in ['echo','arp'] else 'pluck'
                if name in ['acid','psy']:pitch-=12
                amp=.14 if name in ['echo','arp','glitch','filter','stutter'] else .085
                if name=='sub':amp=.04
                put(music,voice(pitch,BEAT*.7,kind,.35*np.sin(step*1.9)),(bar*4+step*.5)*BEAT,amp)
    if name=='kick':bass*=.45
    if name=='wide':music*=1.5
    if name=='drive':bass=np.tanh(bass*2)*.65
    if name=='filter':
        # Smooth musical brightness sweep by crossfading a short moving average.
        width=28; smooth=np.column_stack([np.convolve(music[:,c],np.ones(width)/width,'same') for c in range(2)])
        sweep=(.5+.5*np.sin(np.arange(N)/SR*2*np.pi/(BEAT*8)))[:,None]
        music=smooth*(1-sweep)+music*sweep
    if name in ['glitch','stutter']:
        # Clean beat-synchronous repeats with short crossfades to avoid clicks.
        for b in [3,7,11,15]:
            unit=int(BEAT/(4 if name=='glitch' else 8)*SR);start=int(b*BEAT*SR)
            grain=music[start:start+unit].copy();fade=min(180,len(grain)//4)
            grain[:fade]*=np.linspace(0,1,fade)[:,None];grain[-fade:]*=np.linspace(1,0,fade)[:,None]
            for j in range(4 if name=='glitch' else 8):music[start+j*unit:start+(j+1)*unit]=grain
    music=delay(music,.38 if name=='echo' else .17)
    if name in ['pump','wide']:
        phase=(np.arange(N)/SR/BEAT)%1
        duck=(.12+.88*(1-np.exp(-phase*7)))[:,None];music*=duck
    if name=='riser':
        t=np.arange(N)/SR;p=np.clip(t/(BEAT*15),0,1)
        noise=rng.normal(size=N);noise=np.r_[0,np.diff(noise)]*.018
        rise=(noise+np.sin(2*np.pi*np.cumsum(220*2**(p*2))/SR)*.025)*p**2
        rise[t>BEAT*15]=0;music+=rise[:,None]
        for b in np.arange(12,15,.25):put(mix,DRUM['clap'],b*BEAT,.12)
    mix+=bass+music
    # Remove DC, gently round peaks, then normalize with headroom.
    mix-=mix.mean(axis=0);mix=np.tanh(mix*1.12)
    rms=np.sqrt(np.mean(mix**2));gain=min(.82/max(abs(mix).max(),1e-9),.14/max(rms,1e-9));mix*=gain
    fade=int(.012*SR);mix[:fade]*=np.linspace(0,1,fade)[:,None]
    tail=int(.8*SR);mix[-tail:]*=np.linspace(1,0,tail)[:,None]
    assert np.isfinite(mix).all() and abs(mix).max()<.99
    with wave.open(str(OUT/(name+'.wav')),'wb') as w:
        w.setnchannels(2);w.setsampwidth(2);w.setframerate(SR);w.writeframes((mix*32767).astype('<i2').tobytes())
    return {'name':name,'seconds':round(N/SR,2),'peak_db':round(20*np.log10(abs(mix).max()),1),'rms_db':round(20*np.log10(np.sqrt(np.mean(mix**2))),1)}
if __name__=='__main__':
    OUT.mkdir(exist_ok=True)
    print(json.dumps([arrange(k) for k in ['wide','kick','psy','sub','riser','glitch','pump','echo','drive','filter','acid','stutter','arp']],indent=2))
