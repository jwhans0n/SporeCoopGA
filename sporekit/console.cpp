// Devkit console: TCP line protocol on 127.0.0.1, command registry, main-thread queue.
//
// Request : one text line, e.g.  mem.read 0x401000 16
// Response: one JSON line {"ok":true,"out":"..."}
#include <winsock2.h>
#include <ws2tcpip.h>

#include "core.h"
#include "task_pump_metrics.h"
#include "outside_tasks.h"

#include <deque>
#include <map>
#include <memory>
#include <mutex>

namespace sk {

// ---------------- main thread tasks ----------------
static std::mutex g_task_mu;
static std::deque<std::function<void()>> g_tasks;
static DWORD g_main_tid = 0;

void set_main_thread() { g_main_tid = GetCurrentThreadId(); }
DWORD main_thread_id() { return g_main_tid; }

void run_on_main(std::function<void()> fn)
{
    {
        std::lock_guard<std::mutex> lk(g_task_mu);
        g_tasks.push_back(std::move(fn));
    }
    // also wake the window procedure: works during loading screens / broken frames
    if (GetCurrentThreadId() != g_main_tid)
        if (HWND w = (HWND)game_window())
            PostMessageW(w, WM_SK_PUMP, 0, 0);
}

// tasks that must not run inside IDirect3DDevice9::Present (DXVK holds its locks there):
// anything that may create D3D resources, e.g. Spore UI windows (fonts, textures)
static outside_tasks::Queue g_out_tasks;
static task_pump_metrics::Recorder g_outside_metrics;

task_pump_metrics::Snapshot outside_pump_metrics(const std::string& action)
{
    if(action=="start"){
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        return g_outside_metrics.start(uint64_t(frequency.QuadPart));
    }
    if(action=="stop")return g_outside_metrics.stop();
    return g_outside_metrics.snapshot();
}

void run_outside_frame(std::function<void()> fn)
{
    g_out_tasks.push(std::move(fn));
    if (HWND w = (HWND)game_window())
        PostMessageW(w, WM_SK_PUMP_OUT, 0, 0);
}

void pump_outside_tasks()
{
    if (in_frame()) { // called from a message dispatched inside Present: try again later
        g_outside_metrics.defer();
        if (HWND w = (HWND)game_window())
            PostMessageW(w, WM_SK_PUMP_OUT, 0, 0);
        return;
    }
    task_pump_metrics::Ticket measured;
    if(g_outside_metrics.armed()){
        LARGE_INTEGER begin{};QueryPerformanceCounter(&begin);
        measured=g_outside_metrics.begin(frame_index(),uint64_t(begin.QuadPart));
    }
    outside_tasks::Result result;
    struct Measure {
        task_pump_metrics::Ticket ticket;outside_tasks::Result& result;
        ~Measure(){
            const auto left=g_out_tasks.size();
            if(left)if(HWND window=(HWND)game_window())PostMessageW(window,WM_SK_PUMP_OUT,0,0);
            if(!ticket.generation)return;
            LARGE_INTEGER end{};QueryPerformanceCounter(&end);
            g_outside_metrics.finish(ticket,uint64_t(end.QuadPart),result.processed,result.cap_reached,left,result.budget_reached);
        }
    } measure{measured,result};
    static const uint64_t budget=[] {
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        return uint64_t(frequency.QuadPart)/125; // 8 ms, checked between callbacks.
    }();
    outside_tasks::pump(g_out_tasks,result,[]{LARGE_INTEGER now{};QueryPerformanceCounter(&now);return uint64_t(now.QuadPart);},budget);
}

struct DelayedTask {
    uint64_t due;
    std::function<void()> fn;
};
static std::vector<DelayedTask> g_delayed;

void run_after_frames(int frames, std::function<void()> fn)
{
    std::lock_guard<std::mutex> lk(g_task_mu);
    g_delayed.push_back({frame_index() + (uint64_t)(frames > 0 ? frames : 0), std::move(fn)});
}

void pump_main_tasks()
{
    std::vector<std::function<void()>> due;
    {
        std::lock_guard<std::mutex> lk(g_task_mu);
        uint64_t f = frame_index();
        for (size_t i = 0; i < g_delayed.size();) {
            if (g_delayed[i].due <= f) {
                due.push_back(std::move(g_delayed[i].fn));
                g_delayed.erase(g_delayed.begin() + i);
            } else
                i++;
        }
    }
    for (auto& fn : due)
        fn();
    for (int guard = 0; guard < 256; guard++) {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lk(g_task_mu);
            if (g_tasks.empty())
                return;
            fn = std::move(g_tasks.front());
            g_tasks.pop_front();
        }
        fn();
    }
}

// ---------------- command registry ----------------
struct Cmd {
    SkMod* mod;
    std::string help;
    SkCommandFn fn;
    void* user;
    int flags;
};
static std::mutex g_cmd_mu;
static std::map<std::string, Cmd> g_cmds;

int command_add(SkMod* mod, const char* name, const char* help, SkCommandFn fn, void* user,
                int flags)
{
    std::lock_guard<std::mutex> lk(g_cmd_mu);
    g_cmds[name] = Cmd{mod, help ? help : "", fn, user, flags};
    return 0;
}

void commands_remove_all(SkMod* mod)
{
    std::lock_guard<std::mutex> lk(g_cmd_mu);
    for (auto it = g_cmds.begin(); it != g_cmds.end();)
        if (it->second.mod == mod)
            it = g_cmds.erase(it);
        else
            ++it;
}

std::vector<std::pair<std::string, std::string>> commands_list()
{
    std::lock_guard<std::mutex> lk(g_cmd_mu);
    std::vector<std::pair<std::string, std::string>> v;
    for (auto& [n, c] : g_cmds)
        v.emplace_back(n, c.help);
    return v;
}

std::vector<std::string> split_args(const std::string& line)
{
    std::vector<std::string> out;
    std::string cur;
    bool inq = false, have = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (inq) {
            if (c == '\\' && i + 1 < line.size()) {
                cur += line[++i];
            } else if (c == '"')
                inq = false;
            else
                cur += c;
        } else if (c == '"') {
            inq = true;
            have = true;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (have || !cur.empty())
                out.push_back(cur);
            cur.clear();
            have = false;
        } else
            cur += c;
    }
    if (have || !cur.empty())
        out.push_back(cur);
    return out;
}

static int run_cmd(const Cmd& c, const std::vector<std::string>& args, std::string& out)
{
    std::vector<const char*> argv;
    for (auto& a : args)
        argv.push_back(a.c_str());
    Out o;
    int r = c.fn(c.user, (int)argv.size(), argv.data(), reinterpret_cast<SkOut*>(&o));
    out += o.text;
    return r;
}

// Executes a full line. If from_main is false and the command needs the main
// thread, the call is marshalled and waited for.
int command_exec_line(const std::string& line, std::string& out, bool from_main)
{
    auto args = split_args(line);
    if (args.empty())
        return 0;
    Cmd c;
    {
        std::lock_guard<std::mutex> lk(g_cmd_mu);
        auto it = g_cmds.find(args[0]);
        if (it == g_cmds.end()) {
            out += "unknown command: " + args[0] + " (try 'help')";
            return 1;
        }
        c = it->second;
    }
    if (from_main || (c.flags & SK_CMD_ANY_THREAD))
        return run_cmd(c, args, out);

    struct Req {
        HANDLE ev;
        std::string out;
        int rc = -1;
        std::vector<std::string> args;
        std::string name;
    };
    auto req = std::make_shared<Req>();
    req->ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    req->args = args;
    req->name = args[0];
    auto post = (c.flags & SK_CMD_OUTSIDE_FRAME) ? run_outside_frame : run_on_main;
    post([req] {
        Cmd cc;
        {
            std::lock_guard<std::mutex> lk(g_cmd_mu);
            auto it = g_cmds.find(req->name);
            if (it == g_cmds.end()) {
                req->out = "command vanished";
                SetEvent(req->ev);
                return;
            }
            cc = it->second;
        }
        req->rc = run_cmd(cc, req->args, req->out);
        SetEvent(req->ev);
    });
    DWORD w = WaitForSingleObject(req->ev, 60000);
    if (w != WAIT_OBJECT_0) {
        out += "timeout: main thread did not run the command within 60s (game loading or "
               "no frames yet)";
        return 2; // req stays alive via shared_ptr in the queued lambda
    }
    CloseHandle(req->ev);
    out += req->out;
    return req->rc;
}

// ---------------- TCP server ----------------
// One thread, select() over all clients: no thread is created per connection (thread
// start-up can stall in a second Wine instance while the first one runs).
static SOCKET g_listen = INVALID_SOCKET;

struct Client {
    SOCKET s;
    std::string in;
    // in-flight request (requests of one client are answered in order)
    std::shared_ptr<std::string> pending_out;
    std::shared_ptr<int> pending_rc;
    std::shared_ptr<bool> pending_done;
    std::deque<std::string> queue;
    std::string outbuf;
};

static void send_all(SOCKET s, const std::string& d)
{
    size_t off = 0;
    while (off < d.size()) {
        int r = send(s, d.data() + off, (int)(d.size() - off), 0);
        if (r <= 0)
            return;
        off += r;
    }
}

static std::string json_reply(int rc, const std::string& out)
{
    return std::string("{\"ok\":") + (rc == 0 ? "true" : "false") + ",\"rc\":" + std::to_string(rc) +
           ",\"out\":\"" + json_escape(out) + "\"}\n";
}

static std::mutex g_done_mu; // protects pending_* written by the main thread

static void start_next(Client& c)
{
    while (!c.pending_done && !c.queue.empty()) {
        std::string line = c.queue.front();
        c.queue.pop_front();
        auto args = split_args(line);
        if (args.empty())
            continue;
        Cmd cmd;
        bool found;
        {
            std::lock_guard<std::mutex> lk(g_cmd_mu);
            auto it = g_cmds.find(args[0]);
            found = it != g_cmds.end();
            if (found)
                cmd = it->second;
        }
        if (!found) {
            c.outbuf += json_reply(1, "unknown command: " + args[0] + " (try 'help')");
            continue;
        }
        if (cmd.flags & SK_CMD_ANY_THREAD) {
            std::string out;
            int rc = run_cmd(cmd, args, out);
            c.outbuf += json_reply(rc, out);
            continue;
        }
        auto out = std::make_shared<std::string>();
        auto rc = std::make_shared<int>(-1);
        auto done = std::make_shared<bool>(false);
        c.pending_out = out;
        c.pending_rc = rc;
        c.pending_done = done;
        std::string name = args[0];
        auto post = (cmd.flags & SK_CMD_OUTSIDE_FRAME) ? run_outside_frame : run_on_main;
        post([out, rc, done, args, name] {
            Cmd cc;
            bool ok;
            {
                std::lock_guard<std::mutex> lk(g_cmd_mu);
                auto it = g_cmds.find(name);
                ok = it != g_cmds.end();
                if (ok)
                    cc = it->second;
            }
            std::string o;
            int r = ok ? run_cmd(cc, args, o) : 1;
            std::lock_guard<std::mutex> lk(g_done_mu);
            *out = ok ? o : "command vanished";
            *rc = r;
            *done = true;
        });
    }
}

static DWORD WINAPI server_thread(void*)
{
    // Non-blocking sockets polled every few ms: Wine's select()/blocking close can
    // stall for tens of seconds in a second game process sharing the wineserver.
    std::vector<Client> clients;
    SKLOG(SK_DEBUG, "console thread %lu running", GetCurrentThreadId());
    u_long nb = 1;
    ioctlsocket(g_listen, FIONBIO, &nb);
    for (;;) {
        bool idle = true;
        SOCKET s = accept(g_listen, nullptr, nullptr);
        if (s != INVALID_SOCKET) {
            idle = false;
            ioctlsocket(s, FIONBIO, &nb);
            if (clients.size() < 32)
                clients.push_back(Client{s});
            else
                closesocket(s);
        }
        for (size_t i = 0; i < clients.size();) {
            Client& c = clients[i];
            bool dead = false;
            char tmp[4096];
            int r = recv(c.s, tmp, sizeof tmp, 0);
            if (r == 0)
                dead = true;
            else if (r < 0) {
                int e = WSAGetLastError();
                if (e != WSAEWOULDBLOCK)
                    dead = true;
            } else {
                idle = false;
                c.in.append(tmp, r);
                size_t nl;
                while ((nl = c.in.find('\n')) != std::string::npos) {
                    std::string line = c.in.substr(0, nl);
                    c.in.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (!line.empty())
                        c.queue.push_back(line);
                }
            }
            if (c.pending_done) {
                std::lock_guard<std::mutex> lk(g_done_mu);
                if (*c.pending_done) {
                    c.outbuf += json_reply(*c.pending_rc, *c.pending_out);
                    c.pending_done.reset();
                    c.pending_out.reset();
                    c.pending_rc.reset();
                }
            }
            start_next(c);
            while (!c.outbuf.empty()) {
                int w = send(c.s, c.outbuf.data(), (int)c.outbuf.size(), 0);
                if (w > 0)
                    c.outbuf.erase(0, w);
                else if (WSAGetLastError() == WSAEWOULDBLOCK)
                    break;
                else {
                    dead = true;
                    break;
                }
            }
            // keep the connection until the answer of an in-flight request was sent
            if (dead && !c.pending_done) {
                closesocket(c.s);
                clients.erase(clients.begin() + i);
            } else
                i++;
        }
        if (idle)
            Sleep(5);
    }
    return 0;
}

bool console_start(int port)
{
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
        SKLOG(SK_ERROR, "WSAStartup failed");
        return false;
    }
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET)
        return false;
    BOOL yes = TRUE;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g_listen, (sockaddr*)&a, sizeof a) != 0 || listen(g_listen, 8) != 0) {
        SKLOG(SK_ERROR, "console: cannot listen on 127.0.0.1:%d (err %d)", port, WSAGetLastError());
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return false;
    }
    HANDLE t = CreateThread(nullptr, 0, server_thread, nullptr, 0, nullptr);
    if (t)
        CloseHandle(t);
    SKLOG(SK_INFO, "console listening on 127.0.0.1:%d", port);
    return true;
}

} // namespace sk
