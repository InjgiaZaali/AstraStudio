"""Optional local HTTP smoke test. Run while astra-studio --no-browser is active."""
import json
import sys
from urllib.request import Request, urlopen

base = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:4747"
for path, snippet in [("/", b"Astra Studio"), ("/style.css", b".app-shell"), ("/app.js", b"EXAMPLES")]:
    with urlopen(base + path, timeout=5) as response:
        body = response.read()
        assert response.status == 200 and snippet in body, (path, response.status)
for route in ["run", "analyze"]:
    request = Request(base + "/api/" + route, data=b'print(6 * 7);', method="POST",
                      headers={"Content-Type": "text/plain"})
    with urlopen(request, timeout=5) as response:
        content = json.loads(response.read())
    assert content["ok"] and "CALL" in content["disassembly"]
    assert (content["output"] == "42\n") if route == "run" else (content["output"] == "")
print("HTTP smoke test passed")
