#!/usr/bin/env python3
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Build Tool
#
# 역할: 앱스토어 서버 콘텐츠와 앱 서명 — 패키지, 봉인, 개발자 인증서·프로필, 카탈로그
# 수행범위: package: 앱 디렉터리 → <id>-<version>.zpkg (결정적 zip, 숨김 파일 제외)
#             + 봉인 .zyl-seal/seal.json(파일마다 SHA-256)·seal.sig. 공식 앱은
#             스토어 키가, 개발 앱은 개발자 키가 봉인하고 개발자 인증서·프로필을 함께 담는다
#           dev-cert: 개발자 공개키에 스토어 키로 서명한 인증서(만료 있음)
#           profile: 인증서·앱 id 패턴·기기 ID 목록에 스토어 키로 서명한 프로필(만료 있음)
#           catalog: .zpkg 들 → catalog.json(크기·SHA-256·서명, 폐기된 개발자 인증서) + .sig
#           서명은 모두 OTA 와 같은 방식(파일 SHA-256 에 대한 RSA-SHA256). 기기는
#           runtime/services/appstore(app_seal.c), 에뮬레이터는 apps/system/services/appstore.js
#           가 검증한다. 결과 디렉터리를 아무 https 호스트에 올리면 스토어가 된다
# 의존방향: Python 표준 라이브러리, openssl
# SOLID: SRP — 스토어 콘텐츠 생성만 담당
#
# 사용법 (스토어 운영자):
#   openssl genrsa -out store.key 3072 && openssl rsa -in store.key -pubout -out store.pub
#     (store.pub 의 PEM 이 system/store.json 의 publicKey, url 은 올린 catalog.json 주소)
#   python3 -I tools/build-store.py package --key store.key apps-src/hello out/
#   python3 -I tools/build-store.py catalog --key store.key [--revoked revoked.txt] out/
#   python3 -I tools/build-store.py dev-cert --key store.key --developer acme --name "ACME" \
#       --pubkey acme.pub --days 365 --out acme.cert.json
#   python3 -I tools/build-store.py profile --key store.key --cert acme.cert.json \
#       --app 'com.acme.*' --device <기기 ID> --days 30 --out acme.profile.json
# 사용법 (개발자, 등록된 기기에서 실행할 개발 빌드):
#   python3 -I tools/build-store.py package --key acme.key --cert acme.cert.json \
#       --profile acme.profile.json my-app/ out/
# ──────────────────────────────────────────────────────────
import argparse, base64, hashlib, json, os, re, subprocess, sys, time, zipfile

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


def sign_bytes(data, key):
    """RSA-SHA256 over the SHA-256 of data (what the device verifies)"""
    sig = subprocess.run(['openssl', 'dgst', '-sha256', '-sign', key], input=hashlib.sha256(data).digest(),
                         capture_output=True, check=True).stdout
    return base64.b64encode(sig).decode()


def write_signed(path, obj, key):
    """<path> (canonical JSON) and <path>.sig"""
    data = json.dumps(obj, sort_keys=True, separators=(',', ':')).encode()
    with open(path, 'wb') as f:
        f.write(data)
    with open(path + '.sig', 'w') as f:
        f.write(sign_bytes(data, key) + '\n')
    return data


def package(app_dir, out_dir, key, cert=None, profile=None):
    m = manifest_of(app_dir)
    if bool(cert) != bool(profile):
        sys.exit('✗ a development build needs both --cert and --profile')
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
    contents = {rel: open(os.path.join(app_dir, rel), 'rb').read() for rel in files}
    # The seal: every file's hash, signed — the device checks each file it serves
    seal = {'version': 1, 'id': m['id'], 'appVersion': m['version'],
            'files': {rel: hashlib.sha256(data).hexdigest() for rel, data in contents.items()}}
    seal_data = json.dumps(seal, sort_keys=True, separators=(',', ':')).encode()
    contents['.zyl-seal/seal.json'] = seal_data
    contents['.zyl-seal/seal.sig'] = (sign_bytes(seal_data, key) + '\n').encode()
    if cert:
        for name, src in (('cert', cert), ('profile', profile)):
            contents['.zyl-seal/%s.json' % name] = open(src, 'rb').read()
            contents['.zyl-seal/%s.sig' % name] = open(src + '.sig', 'rb').read()
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for rel in sorted(contents):
            info = zipfile.ZipInfo(rel, STAMP)
            info.external_attr = 0o100644 << 16          # a plain file
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, contents[rel])
    print(out)


def dev_cert(key, developer, name, pubkey, days, out):
    if not re.match(r'^[A-Za-z0-9][A-Za-z0-9._-]*$', developer):
        sys.exit('✗ developer id: letters, digits, . _ -')
    write_signed(out, {'version': 1, 'developer': developer, 'name': name,
                       'publicKey': open(pubkey).read(), 'notAfter': int(time.time()) + days * 86400}, key)
    print('%s (+.sig), sha256 %s' % (out, hashlib.sha256(open(out, 'rb').read()).hexdigest()))


def profile(key, cert, apps, devices, days, out):
    for a in apps:
        if not re.match(r'^[A-Za-z0-9][A-Za-z0-9._-]*(\.\*)?$', a):
            sys.exit('✗ app pattern: an id, or a prefix ending in .*: %r' % a)
    write_signed(out, {'version': 1, 'cert': hashlib.sha256(open(cert, 'rb').read()).hexdigest(),
                       'apps': apps, 'devices': devices, 'notAfter': int(time.time()) + days * 86400}, key)
    print(out + ' (+.sig)')


def sign(path, key):
    return sign_bytes(open(path, 'rb').read(), key)


def catalog(out_dir, key, revoked_file=None):
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
    # Developer certificates no longer trusted (their sha256, one per line)
    revoked = [l.strip().lower() for l in open(revoked_file)] if revoked_file else []
    revoked = [r for r in revoked if re.match(r'^[0-9a-f]{64}$', r)]
    cat = os.path.join(out_dir, 'catalog.json')
    with open(cat, 'w', encoding='utf-8') as f:
        json.dump({'version': 1, 'apps': apps, 'revoked': revoked}, f, ensure_ascii=False, indent=1)
    with open(cat + '.sig', 'w') as f:
        f.write(sign(cat, key) + '\n')
    print('%s (%d apps) + catalog.json.sig' % (cat, len(apps)))


def main():
    ap = argparse.ArgumentParser(description='Zyl OS app store content and app signing')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('package')
    p.add_argument('--key', required=True, help='seals the package: the store key, or a developer key')
    p.add_argument('--cert', help='developer certificate (with its .sig), for a development build')
    p.add_argument('--profile', help='provisioning profile (with its .sig), for a development build')
    p.add_argument('app_dir')
    p.add_argument('out_dir')
    c = sub.add_parser('catalog')
    c.add_argument('--key', required=True, help='the store private key (PEM)')
    c.add_argument('--revoked', help='revoked developer certificates, one sha256 per line')
    c.add_argument('out_dir')
    d = sub.add_parser('dev-cert')
    d.add_argument('--key', required=True, help='the store private key (PEM)')
    d.add_argument('--developer', required=True)
    d.add_argument('--name', required=True)
    d.add_argument('--pubkey', required=True, help="the developer's public key (PEM)")
    d.add_argument('--days', type=int, default=365)
    d.add_argument('--out', required=True)
    r = sub.add_parser('profile')
    r.add_argument('--key', required=True, help='the store private key (PEM)')
    r.add_argument('--cert', required=True)
    r.add_argument('--app', action='append', required=True, help='an app id, or a prefix like com.acme.*')
    r.add_argument('--device', action='append', required=True, help='a device ID (Settings > About)')
    r.add_argument('--days', type=int, default=30)
    r.add_argument('--out', required=True)
    a = ap.parse_args()
    if a.cmd == 'package':
        package(a.app_dir, a.out_dir, a.key, a.cert, a.profile)
    elif a.cmd == 'catalog':
        catalog(a.out_dir, a.key, a.revoked)
    elif a.cmd == 'dev-cert':
        dev_cert(a.key, a.developer, a.name, a.pubkey, a.days, a.out)
    else:
        profile(a.key, a.cert, a.app, a.device, a.days, a.out)


if __name__ == '__main__':
    main()
