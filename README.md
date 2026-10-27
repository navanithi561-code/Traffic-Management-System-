# 🚦 Traffic Management Simulator
**A DSA-powered smart traffic control system with a C backend.**

## Description & problem statement
Junctions waste time when signals ignore queue length and emergency vehicles. This project simulates a four-way junction and an 8-intersection road network. All logic (queues, priority handling, signal control, routing) runs in **C**; the browser only displays API results.

## Objectives
Apply queues, heaps and graphs to a real problem · give emergency vehicles priority without starving normal traffic · find routes with BFS and Dijkstra · expose everything through a REST API.

## Features
Live junction with N/S/E/W signals and queues · add cars, bikes, buses, trucks, ambulances, fire engines, police · emergency priority levels 1-5 · route finder with highlighted path · analytics charts from real API data · speed control · duplicate-click protection, loading indicators, error toasts, offline detection.

## Tech stack
C (GCC, C11/GNU) with a **self-written HTTP server (sockets / Winsock) and mini JSON writer/parser** - no Mongoose or cJSON, so nothing to download. Frontend: HTML, CSS, vanilla JS, SVG, Chart.js (CDN).

## Project structure
```
backend/include  *.h        backend/src  *.c  (traffic, queue, priority_queue, graph, simulation, api, main)
backend/Makefile            tests/test_core.c      frontend/index.html (HTML+CSS+JS in one file)
```

## Setup (beginner friendly)
**Linux:** `sudo apt install build-essential make`
**Windows:** install MinGW-w64 (e.g. via MSYS2: `pacman -S mingw-w64-ucrt-x86_64-gcc make`) and add it to PATH.

```
cd backend
make                      # builds traffic_server (traffic_server.exe on Windows)
./traffic_server          # Windows: traffic_server.exe
```
Open **http://localhost:8080** - the C server serves the frontend too (same origin, so no CORS needed).
Without `make` on Windows: `gcc -std=gnu11 -Iinclude -o traffic_server.exe src/*.c -lws2_32`

Options: `--port 9000`, `--page path/to/index.html`, `--cors http://localhost:5500` (only if you open the frontend from another dev server; by default **no CORS header** is sent and the server listens on 127.0.0.1 only).
If you open `frontend/index.html` directly as a file, it talks to `http://localhost:8080` and you must start the server with `--cors null`.

## Tests
```
cd backend
make test         # 60 checks: queue, heap, tie-breaks, BFS, Dijkstra, unreachable/invalid, signals, fairness, stats, API errors
make test-asan    # same tests with AddressSanitizer + UBSan (Linux/macOS)
```

## REST API
| Method | Endpoint | Purpose |
|---|---|---|
| GET | /api/health | backend status |
| GET | /api/dashboard | stats + status |
| GET | /api/traffic | signals, queues, passing vehicle |
| GET | /api/network | intersections and roads |
| POST | /api/vehicles | `{"type":"car","direction":"north"}` → 201 |
| GET/POST | /api/emergency | list waiting / add (`priority` 1-5 optional) |
| POST | /api/traffic/process | process next eligible vehicle (409 if none) |
| POST | /api/simulation/start · pause · reset · speed | control (`{"speed":1-10}`) |
| GET | /api/routes/bfs?from=A&to=H | fewest roads |
| GET | /api/routes/shortest?from=A&to=H | minimum distance (Dijkstra) |
| GET | /api/analytics | metrics + chart data |

Types: `car bike bus truck ambulance fire_engine police`. Directions: `north south east west`. Errors: `{"ok":false,"error":"..."}` with 400/404/405/409/413/500.

```
$ curl "localhost:8080/api/routes/shortest?from=A&to=H"
{"ok":true,"algorithm":"dijkstra","from":"A","to":"H","edges":4,"distance":18,"path":["A","B","D","G","H"],...}
$ curl "localhost:8080/api/routes/bfs?from=A&to=H"     -> path A,C,E,H  edges 3, distance 21
```
BFS minimises the **number of roads**; Dijkstra minimises **total distance**. They differ for A→H.

## Signal & fairness policy
* Two compatible phases: **North+South** and **East+West**. Signals are derived from one phase variable, so conflicting greens are impossible (also tested over 2000 random ticks).
* Green length = 4 + waiting vehicles (max 12 ticks), yellow = 2. One vehicle per green direction per tick.
* **Emergency:** each direction has a binary max-heap ordered by priority (desc), arrival (asc), id (asc) - a strict total order, so ties are deterministic. A waiting emergency vehicle on a red approach ends the current green early.
* **Fairness:** emergency preemption is limited to 2 consecutive phases; afterwards the signal goes to whichever approach has waiting vehicles. Normal vehicles are FIFO. Test: an endless ambulance stream cannot starve a waiting car.

## Metric definitions
Time is **simulated**: 1 tick = 1 simulated second at 1x. Avg wait = ticks between adding and crossing, averaged over processed vehicles. Throughput = processed per 60 ticks. Max queue = largest single-direction queue seen. Active intersections = road-network nodes while running (only one junction is simulated in detail).

## Complexity
| Structure / algorithm | Operation | Time |
|---|---|---|
| Linked-list queue | enqueue / dequeue | O(1) |
| Binary heap | insert / remove-max | O(log n) |
| Binary heap | peek | O(1) |
| Graph (adjacency list) | storage | O(V + E) space |
| BFS | route | O(V + E) |
| Dijkstra (binary min-heap) | route | O((V + E) log V) |
Space: queues/heaps O(n) in waiting vehicles.

## Limitations
One simulated junction; vehicles all take the same time to cross; the HTTP server is single-threaded and handles one request at a time; JSON parser handles flat objects only; Chart.js needs internet; no persistence.

## Future enhancements
Multiple junctions linked to the graph; traffic-aware Dijkstra weights; turn lanes; persistence; WebSocket updates.

## Troubleshooting
*Cannot bind to port* → use `--port 8081`. *frontend file not found* → run the server from `backend/`. *`make` not found on Windows* → use the gcc command above. *Charts empty* → check internet for Chart.js.

## Screenshots
`docs/dashboard.png` · `docs/live-traffic.png` · `docs/routes.png` (add yours)

## Publish on GitHub
```
git init && git add . && git commit -m "Traffic Management Simulator"
git branch -M main
git remote add origin https://github.com/<you>/traffic-management-simulator.git
git push -u origin main
```
