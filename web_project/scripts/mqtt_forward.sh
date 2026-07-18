#!/usr/bin/env sh
set -eu

HOST="${1:-127.0.0.1}"
PORT="${2:-1883}"
TOPIC="${3:-sensor/+/telemetry}"
TARGET="${4:-http://127.0.0.1:8080/ingest/telemetry}"
TOKEN="${SENSOR_WEB_TOKEN:-}"

echo "subscribing mqtt://${HOST}:${PORT}/${TOPIC}"
echo "forwarding payloads to ${TARGET}"
if [ -n "$TOKEN" ]; then
  echo "using X-Device-Token from SENSOR_WEB_TOKEN"
fi

mosquitto_sub -h "$HOST" -p "$PORT" -t "$TOPIC" | while IFS= read -r payload; do
  if [ -n "$TOKEN" ]; then
    curl -fsS -X POST "$TARGET" \
      -H 'Content-Type: application/json' \
      -H "X-Device-Token: $TOKEN" \
      --data-binary "$payload" >/dev/null || true
  else
    curl -fsS -X POST "$TARGET" \
      -H 'Content-Type: application/json' \
      --data-binary "$payload" >/dev/null || true
  fi
done
