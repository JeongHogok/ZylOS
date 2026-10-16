# 푸시 서버 운영 — push.zyl-os.com

기기는 푸시 서버 하나에 연결 하나를 들고, 모든 앱의 메시지를 그 연결로 받는다
(`org.zylos.Push`, [SECURITY.md §3](SECURITY.md#푸시--앱-서버가-닿는-길)). 프로토콜은
[ntfy](https://ntfy.sh) 이고, 앱 서버 쪽에서 보면 [UnifiedPush](https://unifiedpush.org) 끝점이다.
서버 주소는 이미지의 `system/push.json` 한 곳이다 (빈 `url` 이면 그 이미지는 푸시를 끈다).

## 흐름

1. 앱이 `ZylBridge.registerPush(제목, 본문)` 을 부르면 기기가 앱마다 토픽 `up` + 영숫자 12자를
   만들고 끝점 `https://push.zyl-os.com/<토픽>?up=1` 을 돌려준다.
2. 앱은 그 끝점을 자기 서버에 준다 (예: 텔레그램 `account.registerDevice` 의 `token_type` 4).
3. 앱 서버는 끝점에 `POST`/`PUT` 한다. 기기는 `GET /<토픽들>/json?since=<마지막 id>` 스트림으로
   받는다 — 끊기면 5초부터 5분까지 늘어나는 간격으로 다시 붙고, 그동안 온 것은 `since` 로 받는다.
4. 앱이 떠 있으면 앱이 받고, 아니면 앱이 등록할 때 준 알림이 뜬다.

## 서버 (ntfy)

`/etc/ntfy/server.yml`:

```yaml
base-url: "https://push.zyl-os.com"
listen-http: ":2586"
behind-proxy: true                      # 아래 리버스 프록시 뒤 — 실제 주소는 X-Forwarded-For
cache-file: "/var/cache/ntfy/cache.db"  # 재시작해도 메시지가 남아 기기가 since 로 받는다
cache-duration: "12h"                   # 기기가 그보다 오래 끊겨 있으면 그 사이 것은 잃는다
auth-file: "/var/lib/ntfy/user.db"
auth-default-access: "read-write"       # 토픽 이름(약 71비트)이 곧 비밀 — 계정 없이 읽고 쓴다
visitor-subscriber-rate-limiting: true  # up* 토픽의 한도를 구독자(기기) 쪽으로 — 아래 참고
visitor-subscription-limit: 300         # 기기 하나가 앱마다 토픽 하나를 한 연결로 구독한다
```

- **`visitor-subscriber-rate-limiting` 은 꼭 켠다.** 기본은 발행자 IP 마다 한도를 세므로, 한
  주소에서 많은 사용자에게 보내는 앱 서버(텔레그램 같은)가 곧 막힌다. 켜면 `up` + 12자(모두 14자)
  토픽은 그 토픽을 처음 구독한 기기의 한도로 센다. 기기가 이 꼴로 토픽을 만드는 이유다. 구독자가
  없는 토픽에 보내면 `507` 이 돌아간다.
- **TLS 는 리버스 프록시에서.** 기기는 `https` 만 쓰고(`curl --proto =https`) 인증서를 검증한다.

nginx (스트림이 버퍼에 막히지 않게):

```nginx
server {
  listen 443 ssl http2;
  server_name push.zyl-os.com;
  # ssl_certificate … (Let's Encrypt 등)
  location / {
    proxy_pass http://127.0.0.1:2586;
    proxy_http_version 1.1;
    proxy_buffering off;
    proxy_request_buffering off;
    proxy_set_header Host $http_host;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_connect_timeout 3m;
    proxy_send_timeout 3m;
    proxy_read_timeout 3m;
    client_max_body_size 0;
  }
}
```

설정 키의 뜻과 기본값은 ntfy 문서의 [설정](https://docs.ntfy.sh/config/) 쪽이 권위다.

## 확인

```bash
curl -s https://push.zyl-os.com/v1/health          # {"healthy":true, …}
# 기기 없이 흐름만: 한 터미널에서 구독, 다른 터미널에서 발행
curl -sN https://push.zyl-os.com/upTEST00000000/json
curl -s -d 'hello' https://push.zyl-os.com/upTEST00000000
```

기기 쪽 동작은 `tests/push_e2e.sh`(로컬 HTTPS 의 ntfy 흉내 서버)가 매 빌드 검증한다.

## 하지 않는 것

- **메시지 내용을 읽을 수 있다.** 서버는 앱 서버가 보낸 본문을 그대로 전달한다. 민감한 내용은
  앱이 암호화해서 보내거나(UnifiedPush 의 WebPush 암호화), 텔레그램처럼 "가져갈 것이 있다"는
  신호만 보내고 앱이 자기 연결로 가져온다.
- **기기가 자는 동안은 받지 못한다.** 화면이 꺼지고 약 35분 뒤 절전에 들어가면 연결이 끊기고,
  깨어나면 `since` 로 그동안 온 것을 받는다. 자는 동안 받으려면 보드의 Wi-Fi 웨이크(WoWLAN)가
  필요하다 — 보드 검증 항목.
