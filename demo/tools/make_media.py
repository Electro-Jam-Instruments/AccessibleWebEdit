#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Compose the per-step PPM frames written by demo_render into:
#   - demo.gif          an animated GIF of the editing session (looping)
#   - contact_sheet.png  all frames stacked vertically (static fallback)
#
# Pure Python stdlib (zlib + struct). The demo uses a fixed 4-colour palette,
# so GIF quantisation is exact.
import sys, os, glob, zlib, struct

PALETTE = [(245,245,248),(25,25,35),(210,40,40),(180,205,255)]  # bg, ink, caret, sel

def read_ppm(p):
    d = open(p,'rb').read()
    assert d[:2]==b'P6', p
    idx, vals = 2, []
    while len(vals) < 3:
        while d[idx] in b' \t\n\r': idx += 1
        s = idx
        while d[idx] not in b' \t\n\r': idx += 1
        vals.append(int(d[s:idx]))
    idx += 1
    w,h,_ = vals
    return w, h, d[idx:idx+w*h*3]

def nearest(rgb):
    best, bi = 1<<30, 0
    for i,(r,g,b) in enumerate(PALETTE):
        dd = (rgb[0]-r)**2 + (rgb[1]-g)**2 + (rgb[2]-b)**2
        if dd < best: best, bi = dd, i
    return bi

def indices(w,h,rgb):
    out = bytearray(w*h)
    for i in range(w*h):
        out[i] = nearest(rgb[i*3:i*3+3])
    return out

# ---- GIF (LZW) ----
def lzw(idx, min_code_size):
    clear, eoi = 1<<min_code_size, (1<<min_code_size)+1
    code_size = min_code_size+1
    table = {bytes([i]):i for i in range(1<<min_code_size)}
    nxt = eoi+1
    out, cur, nbits = bytearray(), 0, 0
    def emit(code):
        nonlocal cur, nbits
        cur |= code << nbits; nbits += code_size
        while nbits >= 8:
            out.append(cur & 0xFF); cur >>= 8; nbits -= 8
    emit(clear)
    w = bytes([idx[0]])
    for c in idx[1:]:
        wc = w + bytes([c])
        if wc in table:
            w = wc
        else:
            emit(table[w])
            table[wc] = nxt; nxt += 1
            if nxt == (1<<code_size) and code_size < 12:
                code_size += 1
            w = bytes([c])
    emit(table[w]); emit(eoi)
    if nbits > 0: out.append(cur & 0xFF)
    return bytes(out)

def write_gif(path, w, h, frames, delay_cs=80):
    mcs = 2  # 4 colours
    b = bytearray(b'GIF89a')
    b += struct.pack('<HH', w, h)
    b += bytes([0x80 | (0x10) | 0x01, 0, 0])  # GCT, 4-entry
    for r,g,bl in PALETTE: b += bytes([r,g,bl])
    # NETSCAPE loop forever
    b += b'\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00'
    for idx in frames:
        b += bytes([0x21,0xF9,0x04,0x00]) + struct.pack('<H',delay_cs) + bytes([0,0])
        b += bytes([0x2C]) + struct.pack('<HHHH',0,0,w,h) + bytes([0])
        b += bytes([mcs])
        data = lzw(idx, mcs)
        for i in range(0,len(data),255):
            chunk = data[i:i+255]
            b += bytes([len(chunk)]) + chunk
        b += bytes([0])
    b += bytes([0x3B])
    open(path,'wb').write(b)

# ---- PNG (contact sheet) ----
def write_png(path, w, h, rgb):
    raw = bytearray()
    for y in range(h):
        raw.append(0); raw += rgb[y*w*3:(y+1)*w*3]
    def chunk(t,d):
        c = t+d
        return struct.pack('>I',len(d))+c+struct.pack('>I',zlib.crc32(c)&0xffffffff)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB',w,h,8,2,0,0,0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw),9))
    png += chunk(b'IEND', b'')
    open(path,'wb').write(png)

def main():
    frame_dir, out_dir = sys.argv[1], sys.argv[2]
    paths = sorted(glob.glob(os.path.join(frame_dir,'frame_*.ppm')))
    assert paths, 'no frames'
    dims = read_ppm(paths[0])
    w, h = dims[0], dims[1]
    idx_frames, rgbs = [], []
    for p in paths:
        fw,fh,rgb = read_ppm(p)
        assert (fw,fh)==(w,h), 'frame size mismatch'
        idx_frames.append(indices(w,h,rgb)); rgbs.append(rgb)
    write_gif(os.path.join(out_dir,'demo.gif'), w, h, idx_frames)
    # contact sheet: stack vertically with 6px separators
    gap, gc = 6, bytes([200,200,205])
    sh = h*len(rgbs) + gap*(len(rgbs)-1)
    sheet = bytearray(gc*(w*sh))
    y0 = 0
    for rgb in rgbs:
        for y in range(h):
            sheet[((y0+y)*w)*3:((y0+y)*w+w)*3] = rgb[y*w*3:(y+1)*w*3]
        y0 += h + gap
    write_png(os.path.join(out_dir,'contact_sheet.png'), w, sh, bytes(sheet))
    print('wrote demo.gif (%d frames) and contact_sheet.png  %dx%d' % (len(rgbs),w,h))

if __name__ == '__main__':
    main()
