#!/usr/bin/env python3
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Build Tool
#
# 역할: 앱스토어 서버 콘텐츠 만들기 — 앱 패키지와 서명된 카탈로그
# 수행범위: package: 앱 디렉터리 → <id>-<version>.zpkg (결정적 zip, 숨김 파일 제외)
#           catalog: 디렉터리의 .zpkg 들 → catalog.json(각 패키지 크기·SHA-256·
#           서명) + catalog.json.sig. 서명은 OTA 와 같은 방식(패키지 SHA-256 에 대한
#           RSA-SHA256, tools/build-ota.sh)이고 기기는 runtime/services/appstore 가,
#           에뮬레이터는 apps/system/services/appstore.js 가 검증한다.
#           결과 디렉터리를 아무 https 호스트에 그대로 올리면 스토어가 된다
# 의존방향: Python 표준 라이브러리, openssl
# SOLID: SRP — 스토어 콘텐츠 생성만 담당
#
# 사용법:
#   openssl genrsa -out store.key 3072 && openssl rsa -in store.key -pubout -out store.pub
#     (store.pub 의 PEM 이 system/store.json 의 publicKey, url 은 올린 catalog.json 주소)
#   python3 -I tools/build-store.py package apps-src/hello out/
#   python3 -I tools/build-store.py catalog --key store.key out/
# ──────────────────────────────────────────────────────────
import argparse, base64, hashlib, json, os, re, subprocess, sys, zipfile

ID = re.compile(r'^[A-Za-z0-9][A-Za-z0-9._-]*\.[A-Za-z0-9._-]+$')
VERSION = re.compile(r'^[0-9]+(\.[0-9]+)*$')
STAMP = (2020, 1, 1, 0, 0, 0)   # same bytes for the same app


def manifest_of(app_dir):
    with open(os.path.join(app_dir, 'app.json'), encoding='utf-8') as f:
        m = json.load(f)
    if not ID.match(m.get('id', '')) or '..' in m['id']:
        sys.exit('✗ app.json id must be a reverse-domain name: %r' % m.get('id'))
    if not VERSION.match(m.get('version', '')):
        sys.exit('✗ app.json version must be digits and dots: %r' % m.get('version'))
    if not os.path.isfile(os.path.join(app_dir, m.get('entry', 'index.html'))):
        sys.exit('✗ the entry page is missing')
    return m


def package(app_dir, out_dir):
    m = manifest_of(app_dir)
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, '%s-%s.zpkg' % (m['id'], m['version']))
    files = []
    for root, dirs, names in os.walk(app_dir):
        dirs[:] = sorted(d for d in dirs if not d.startswith('.'))
        for n in sorted(names):
            p = os.path.join(root, n)
            if n.startswith('.') or os.path.islink(p):
                continue
            files.append(os.path.relpath(p, app_dir).replace(os.sep, '/'))
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for rel in files:
            info = zipfile.ZipInfo(rel, STAMP)
            info.external_attr = 0o100644 << 16          # a plain file
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(os.path.join(app_dir, rel), 'rb') as f:
                z.writestr(info, f.read())
    print(out)


def sign(path, key):
    """RSA-SHA256 over the file's SHA-256 (what the device verifies)"""
    digest = hashlib.sha256(open(path, 'rb').read()).digest()
    sig = subprocess.run(['openssl', 'dgst', '-sha256', '-sign', key], input=digest,
                         capture_output=True, check=True).stdout
    return base64.b64encode(sig).decode()


def catalog(out_dir, key):
    apps = []
    for name in sorted(os.listdir(out_dir)):
        if not name.endswith('.zpkg'):
            continue
        path = os.path.join(out_dir, name)
        with zipfile.ZipFile(path) as z:
            m = json.loads(z.read('app.json'))
        data = open(path, 'rb').read()
        apps.append({
            'id': m['id'], 'name': m.get('name', m['id']), 'version': m['version'],
            'description': m.get('description', ''), 'developer': m.get('developer', ''),
            'category': m.get('category', ''), 'permissions': m.get('permissions', []),
            'package': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            'signature': sign(path, key),
        })
    cat = os.path.join(out_dir, 'catalog.json')
    with open(cat, 'w', encoding='utf-8') as f:
        json.dump({'version': 1, 'apps': apps}, f, ensure_ascii=False, indent=1)
    with open(cat + '.sig', 'w') as f:
        f.write(sign(cat, key) + '\n')
    print('%s (%d apps) + catalog.json.sig' % (cat, len(apps)))


def main():
    ap = argparse.ArgumentParser(description='Zyl OS app store content')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('package')
    p.add_argument('app_dir')
    p.add_argument('out_dir')
    c = sub.add_parser('catalog')
    c.add_argument('--key', required=True, help='the store private key (PEM)')
    c.add_argument('out_dir')
    a = ap.parse_args()
    if a.cmd == 'package':
        package(a.app_dir, a.out_dir)
    else:
        catalog(a.out_dir, a.key)


if __name__ == '__main__':
    main()
