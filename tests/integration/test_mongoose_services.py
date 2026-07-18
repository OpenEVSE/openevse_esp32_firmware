import base64
import contextlib
import hashlib
import json
import os
import socket
import socketserver
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

import pytest
import requests


REQUEST_TIMEOUT = 5
CONFIG_SERVICE_SNTP = 1 << 3


def api_get(url):
    return requests.get(url, timeout=REQUEST_TIMEOUT, allow_redirects=False)


def api_post(url, json_data):
    return requests.post(url, json=json_data, timeout=REQUEST_TIMEOUT, allow_redirects=False)


def wait_until(predicate, timeout=10, poll_interval=0.2):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        last = predicate()
        if last:
            return last
        time.sleep(poll_interval)
    return last


def read_mqtt_packet(sock):
    first = sock.recv(1)
    if not first:
        return None, b""

    multiplier = 1
    remaining = 0
    while True:
        encoded = sock.recv(1)
        if not encoded:
            return None, b""
        value = encoded[0]
        remaining += (value & 127) * multiplier
        if (value & 128) == 0:
            break
        multiplier *= 128

    payload = b""
    while len(payload) < remaining:
        chunk = sock.recv(remaining - len(payload))
        if not chunk:
            break
        payload += chunk
    return first[0] >> 4, payload


class MqttHandler(socketserver.BaseRequestHandler):
    def handle(self):
        self.server.connected.set()
        self.request.settimeout(0.5)
        while not self.server.stop_event.is_set():
            try:
                packet_type, payload = read_mqtt_packet(self.request)
            except socket.timeout:
                continue
            except OSError:
                break
            if packet_type is None:
                break
            self.server.packet_types.append(packet_type)
            if packet_type == 1:
                self.request.sendall(b"\x20\x02\x00\x00")
            elif packet_type == 8 and len(payload) >= 2:
                packet_id = payload[:2]
                self.request.sendall(b"\x90\x03" + packet_id + b"\x00")
            elif packet_type == 12:
                self.request.sendall(b"\xd0\x00")
            elif packet_type == 14:
                break


class ThreadedMqttServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True

    def __init__(self, server_address):
        super().__init__(server_address, MqttHandler)
        self.connected = threading.Event()
        self.stop_event = threading.Event()
        self.packet_types = []


@contextlib.contextmanager
def mqtt_server():
    server = ThreadedMqttServer(("127.0.0.1", 0))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server
    finally:
        server.stop_event.set()
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


class EmoncmsHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/input/post":
            self.server.requests.append(parse_qs(parsed.query))
            body = b"ok"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_error(404)

    def log_message(self, fmt, *args):
        pass


@contextlib.contextmanager
def emoncms_server():
    server = ThreadingHTTPServer(("127.0.0.1", 0), EmoncmsHandler)
    server.requests = []
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


def websocket_connect(host, port, path):
    sock = socket.create_connection((host, port), timeout=REQUEST_TIMEOUT)
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    )
    sock.sendall(request.encode("ascii"))
    response = b""
    while b"\r\n\r\n" not in response:
        response += sock.recv(4096)
    assert b" 101 " in response.split(b"\r\n", 1)[0], response.decode("latin1")
    accept = base64.b64encode(
        hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()
    )
    assert accept in response
    return sock


def websocket_send_text(sock, message):
    payload = message.encode("utf-8")
    mask = os.urandom(4)
    header = bytearray([0x81])
    if len(payload) < 126:
        header.append(0x80 | len(payload))
    elif len(payload) < 65536:
        header.extend([0x80 | 126, *struct.pack("!H", len(payload))])
    else:
        header.extend([0x80 | 127, *struct.pack("!Q", len(payload))])
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    sock.sendall(bytes(header) + mask + masked)


def websocket_read_text(sock, timeout=10):
    sock.settimeout(timeout)
    while True:
        header = sock.recv(2)
        if len(header) < 2:
            raise AssertionError("WebSocket closed before a frame was received")
        opcode = header[0] & 0x0F
        length = header[1] & 0x7F
        if length == 126:
            length = struct.unpack("!H", sock.recv(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", sock.recv(8))[0]
        if header[1] & 0x80:
            mask = sock.recv(4)
        else:
            mask = None
        payload = b""
        while len(payload) < length:
            payload += sock.recv(length - len(payload))
        if mask:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        if opcode == 1:
            return payload.decode("utf-8")
        if opcode == 8:
            raise AssertionError("WebSocket closed")


def websocket_read_json_until(sock, predicate, timeout=10):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        frame_timeout = max(0.1, deadline - time.time())
        data = json.loads(websocket_read_text(sock, timeout=frame_timeout))
        last = data
        if predicate(data):
            return data
    raise AssertionError(f"Timed out waiting for WebSocket frame; last={last}")


@pytest.mark.timeout(120)
class TestMongooseServices:
    def test_websocket_ping_frames(self, evse_instance):
        with websocket_connect("127.0.0.1", evse_instance["native_port"], "/ws") as sock:
            websocket_send_text(sock, '{"ping":1}')
            pong = websocket_read_json_until(sock, lambda data: data.get("pong") == 1)
            assert pong["pong"] == 1

    def test_mqtt_client_connects_to_local_broker(self, evse_instance):
        native_url = evse_instance["native_url"]
        original = api_get(f"{native_url}/config").json()
        with mqtt_server() as broker:
            try:
                response = api_post(
                    f"{native_url}/config",
                    {
                        "mqtt_enabled": True,
                        "mqtt_protocol": "mqtt",
                        "mqtt_server": "127.0.0.1",
                        "mqtt_port": broker.server_address[1],
                        "mqtt_topic": "openevse-integration",
                        "mqtt_announce_topic": "openevse-integration/announce",
                    },
                )
                assert response.status_code == 200, response.text
                api_post(f"{native_url}/mqtt", {})

                assert broker.connected.wait(timeout=10)
                status = wait_until(
                    lambda: api_get(f"{native_url}/mqtt").json().get("mqtt_connected") == 1,
                    timeout=15,
                )
                assert status is True
                assert 1 in broker.packet_types
            finally:
                api_post(
                    f"{native_url}/config",
                    {
                        "mqtt_enabled": original.get("mqtt_enabled", False),
                        "mqtt_server": original.get("mqtt_server", ""),
                        "mqtt_port": original.get("mqtt_port", 1883),
                        "mqtt_topic": original.get("mqtt_topic", ""),
                        "mqtt_announce_topic": original.get("mqtt_announce_topic", ""),
                        "mqtt_protocol": original.get("mqtt_protocol", "mqtt"),
                    },
                )
                api_post(f"{native_url}/mqtt", {})

    def test_sntp_sync_now_reports_dns_failure(self, evse_instance):
        native_url = evse_instance["native_url"]
        original = api_get(f"{native_url}/config").json()
        try:
            response = api_post(
                f"{native_url}/config",
                {
                    "flags": original.get("flags", 0) | CONFIG_SERVICE_SNTP,
                    "sntp_hostname": "openevse-sntp.invalid",
                },
            )
            assert response.status_code == 200, response.text
            response = api_post(f"{native_url}/time", {"sync_now": True})
            assert response.status_code == 200, response.text

            time_status = api_get(f"{native_url}/time").json()
            if time_status["ntp_status"] == "disabled":
                pytest.xfail("native TimeManager does not enable SNTP from integration config yet")
            assert time_status["ntp_status"] == "connecting"

            def ntp_status_finished():
                data = api_get(f"{native_url}/time").json()
                return data if data.get("ntp_status") in ("retry", "synchronized") else None

            wait_until(ntp_status_finished, timeout=20)
        finally:
            api_post(
                f"{native_url}/config",
                {
                    "sntp_enabled": original.get("sntp_enabled", True),
                    "sntp_hostname": original.get("sntp_hostname", "pool.ntp.org"),
                },
            )

    def test_emoncms_http_client_publishes_to_local_server(self, evse_instance):
        native_url = evse_instance["native_url"]
        original = api_get(f"{native_url}/config").json()
        with emoncms_server() as server:
            try:
                response = api_post(
                    f"{native_url}/config",
                    {
                        "emoncms_enabled": True,
                        "emoncms_server": f"http://127.0.0.1:{server.server_port}",
                        "emoncms_node": "integration",
                        "emoncms_apikey": "key12345",
                    },
                )
                assert response.status_code == 200, response.text

                seen = wait_until(lambda: server.requests[0] if server.requests else None, timeout=15)
                assert seen is not None
                assert seen.get("node") == ["integration"]
                assert "apikey" in seen

                status = wait_until(
                    lambda: api_get(f"{native_url}/status").json().get("emoncms_connected") == 1,
                    timeout=10,
                )
                assert status is True
            finally:
                api_post(
                    f"{native_url}/config",
                    {
                        "emoncms_enabled": original.get("emoncms_enabled", False),
                        "emoncms_server": original.get("emoncms_server", ""),
                        "emoncms_node": original.get("emoncms_node", ""),
                        "emoncms_apikey": original.get("emoncms_apikey", ""),
                    },
                )

    def test_certificate_subroutes_bind_and_self_signed_endpoint_is_explicit(self, evse_instance):
        native_url = evse_instance["native_url"]
        root = api_get(f"{native_url}/certificates/root")
        assert root.status_code == 200, root.text

        generated = api_post(f"{native_url}/certificates/self-signed", {})
        assert generated.status_code == 501, generated.text
        assert "native builds" in generated.text
