# Public deployment

This stack serves the WALL-E control page and device endpoints at
`walle.kangqing.net`, and the manager at `admin-walle.kangqing.net`.

## Before starting

1. Point both DNS A records to the server.
2. Copy `.env.example` to `.env` and replace every placeholder.
3. Copy the current `data/.config.yaml`, MySQL dump, uploads, and
   `models/SenseVoiceSmall/model.pt` from the office server.
4. In `.config.yaml`, use these public endpoints:

```yaml
server:
  websocket: wss://walle.kangqing.net/xiaozhi/v1/
  vision_explain: https://walle.kangqing.net/mcp/vision/explain
```

## Start

```bash
docker compose config
docker compose build xiaozhi-server
docker compose up -d mysql redis emqx
docker compose up -d xiaozhi-web xiaozhi-server caddy
docker compose ps
```

The EMQX dashboard is bound only to localhost. Use an SSH tunnel when it is
needed:

```bash
ssh -L 18083:127.0.0.1:18083 root@1.92.54.162
```

Then open `http://127.0.0.1:18083`, create a built-in database user for the
robot and web controller, and use a long unique password.
