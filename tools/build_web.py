#!/usr/bin/env python3
"""Compression des fichiers web du pont.

Les sources editables sont dans web_src/ (scripts, index.html). Chaque fichier
y est compresse (gzip niveau 9) vers data/web/<meme chemin>.gz : le serveur web
sert la version .gz quand elle existe (Content-Encoding: gzip), ce qui divise
par 4 a 5 la place occupee dans la partition LittleFS (1,9 Mo).

Appele par la compilation (cible web_gz, avant l'image LittleFS) et par
tools/flash_keep_data.ps1. Un .gz n'est reecrit que si son contenu change
(mtime gzip fixe a 0 : sortie reproductible), et la version non compressee
d'un fichier, si elle traine dans data/web, est supprimee.
"""
import gzip
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'web_src')
DST = os.path.join(ROOT, 'data', 'web')


def main():
    if not os.path.isdir(SRC):
        print('build_web: web_src/ absent, rien a compresser')
        return 0
    total_in = total_out = changed = 0
    for base, _dirs, files in os.walk(SRC):
        for name in sorted(files):
            src = os.path.join(base, name)
            rel = os.path.relpath(src, SRC)
            dst = os.path.join(DST, rel + '.gz')
            with open(src, 'rb') as f:
                raw = f.read()
            data = gzip.compress(raw, compresslevel=9, mtime=0)
            total_in += len(raw)
            total_out += len(data)
            old = None
            if os.path.exists(dst):
                with open(dst, 'rb') as f:
                    old = f.read()
            if old != data:
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, 'wb') as f:
                    f.write(data)
                changed += 1
            plain = os.path.join(DST, rel)
            if os.path.exists(plain):          # ancienne copie non compressee
                os.remove(plain)
    print('build_web: %d Ko -> %d Ko (%d fichier(s) mis a jour)'
          % (total_in // 1024, total_out // 1024, changed))
    return 0


if __name__ == '__main__':
    sys.exit(main())
