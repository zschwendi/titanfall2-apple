// hook_test.cpp -- does the host program hear DXMT?  Proof of patch 4 of the fork (patches/dxmt/0004-*.patch):
// a log function and an abort function, registered once in winemetal.dylib, heard for dxgi, d3d11 and winemetal.
// Interface: third_party/dxmt/include/native/dxmt_native_host.h.
//
// Every case runs in a forked child that loads DXMT itself, registers the two functions and does one thing.  The
// functions write what they were given into a pipe; the parent reads the pipe, the child's stderr and stdout, and
// how the child ended, and checks them.  No window, no game file: the shaders are the made-up ones of make_dxbc.py.
//
//   error-line      CreatePixelShader of a container with no code chunk.  The creator fails; the log function got
//                   an error-level line "Failed to initialize shader: ..." and DXMT also wrote it to stderr.
//   instead         the same with a log function that answers 1: it gets the lines, stderr gets none.
//   level-none      the same with DXMT_LOG_LEVEL=none: the log function still gets the error line, and only it.
//   map-null        Map of a default-usage buffer with a null out-pointer.  The brief and the design name this as
//                   the way to force UNIMPLEMENTED (d3d11_context_imm.cpp:281-284).  It is not: Map returns
//                   E_INVALIDARG for a null out-pointer at its first line (:142-143).  Checked: E_INVALIDARG, no abort.
//   unimplemented   UpdateSubresource into a staging buffer (d3d11_context_impl.cpp:1242): the log function gets the
//                   error line, then the abort function gets the message, then DXMT calls abort() and the child's
//                   SIGABRT handler runs (the catch-all a host needs for the abort() sites that call no function).
//   abort-exits     the same with an abort function that ends the process itself (_exit(77)): status 77, no signal.
//   unknown-op      CreatePixelShader of a shader with an instruction DXMT's converter has no case for.
//                   --kind assert:  the converter's assert fires inside winemetal.dylib; the abort function gets
//                                   "Assertion failed: (0 && "unhandled dxbc instruction") ..."; then SIGABRT.
//                   --kind release: the creator succeeds, nothing reaches either function, and the only trace is the
//                                   line "unhandled dxbc instruction 207" on standard output.  (That is the hole the
//                                   assert build exists to close; the test states it, it cannot fail on it.)
//
// Build:  clang++ -std=c++20 -w -I third_party/dxmt/include/native/windows -I third_party/dxmt/include/native/directx
//             -I third_party/dxmt/include/native -I tools/dxmt tools/dxmt/hook_test.cpp -o <out>
// Run:    <out> <DXMT meson build dir> --kind release|assert        (tools/dxmt/hooks.sh does both)
// Exit:   0 and a last line HOOK_TEST_OK if every check held.
//
// The child catches SIGABRT itself, records it and ends with status 86.  That is what a host does, and it keeps
// macOS from writing a crash report into ~/Library/Logs/DiagnosticReports for every run of this test.
#include "dxmt_load.h"
#include "dxbc_blobs.h"

#include "dxmt_native_host.h"

#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <string>
#include <vector>

// ---- the child ---------------------------------------------------------------------------------------------
static int g_pipe = -1;
static int g_log_answer = 0;
static bool g_abort_exits = false;
static int g_cookie_log = 0, g_cookie_abort = 0;

static void put(const char *tag, const char *text) {
  char buf[1024];
  int n = snprintf(buf, sizeof(buf), "%s %s\n", tag, text);
  if (n >= (int)sizeof(buf)) { n = sizeof(buf) - 1; buf[n - 1] = '\n'; }
  (void)!write(g_pipe, buf, n);
}

static int host_log(void *user, int level, const char *line) {
  char tag[8];
  snprintf(tag, sizeof(tag), "L%d", level);
  put(user == &g_cookie_log ? tag : "BADUSER", line);
  return g_log_answer;
}

static void host_abort(void *user, const char *message) {
  put(user == &g_cookie_abort ? "A" : "BADUSER", message);
  if (g_abort_exits) _exit(77);
}

static void on_sigabrt(int) {
  static const char text[] = "S SIGABRT\n";
  (void)!write(g_pipe, text, sizeof(text) - 1);
  _exit(86);
}

static int child(const std::string &dir, const std::string &what) {
  alarm(30);  // a hung child must not hang the test
  signal(SIGABRT, on_sigabrt);
  setenv("DXMT_LOG_PATH", "none", 1);
  setenv("DXMT_SHADER_CACHE", "0", 1);
  if (what == "level-none") setenv("DXMT_LOG_LEVEL", "none", 1); else unsetenv("DXMT_LOG_LEVEL");
  if (what == "instead") g_log_answer = 1;
  if (what == "abort-exits") g_abort_exits = true;

  Dxmt dx;
  if (!dxmt_load(dir, &dx)) return 2;
  // winemetal.dylib is already loaded as a dependency of the other two; opening it by path returns that image
  std::string wm = dir + "/src/nativemetal/winemetal.dylib";
  if (access(wm.c_str(), R_OK) != 0) wm = dir + "/winemetal.dylib";
  void *h = dlopen(wm.c_str(), RTLD_NOW);
  auto set_log = h ? (void (*)(DXMTHostLogFunction, void *))dlsym(h, "DXMTSetHostLogFunction") : nullptr;
  auto set_abort = h ? (void (*)(DXMTHostAbortFunction, void *))dlsym(h, "DXMTSetHostAbortFunction") : nullptr;
  if (!set_log || !set_abort) { fprintf(stderr, "winemetal.dylib has no DXMTSetHost*Function: the fork is not at patch 4\n"); return 3; }
  set_log(host_log, &g_cookie_log);
  set_abort(host_abort, &g_cookie_abort);

  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0}, fl;
  ID3D11Device *dev = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  HRESULT hr = dx.create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &dev, &fl, &ctx);
  if (FAILED(hr)) { fprintf(stderr, "D3D11CreateDevice hr=0x%08x\n", (unsigned)hr); return 2; }

  char r[64];
  if (what == "error-line" || what == "instead" || what == "level-none") {
    ID3D11PixelShader *ps = nullptr;
    hr = dev->CreatePixelShader(kNoCodeDxbc, sizeof(kNoCodeDxbc), nullptr, &ps);
    snprintf(r, sizeof(r), "0x%08x shader=%s", (unsigned)hr, ps ? "non-null" : "null");
    put("R", r);
  } else if (what == "unknown-op") {
    ID3D11PixelShader *ps = nullptr;
    hr = dev->CreatePixelShader(kPsUnknownDxbc, sizeof(kPsUnknownDxbc), nullptr, &ps);
    snprintf(r, sizeof(r), "0x%08x shader=%s", (unsigned)hr, ps ? "non-null" : "null");
    put("R", r);
    if (ps) ps->Release();
  } else if (what == "map-null") {
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 64; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    ID3D11Buffer *buf = nullptr;
    hr = dev->CreateBuffer(&bd, nullptr, &buf);
    if (FAILED(hr) || !buf) { fprintf(stderr, "CreateBuffer hr=0x%08x\n", (unsigned)hr); return 2; }
    hr = ctx->Map(buf, 0, D3D11_MAP_WRITE, 0, nullptr);
    snprintf(r, sizeof(r), "0x%08x", (unsigned)hr);
    put("R", r);
    buf->Release();
  } else if (what == "unimplemented" || what == "abort-exits") {
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 64; bd.Usage = D3D11_USAGE_STAGING; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
    ID3D11Buffer *buf = nullptr;
    hr = dev->CreateBuffer(&bd, nullptr, &buf);
    if (FAILED(hr) || !buf) { fprintf(stderr, "CreateBuffer(staging) hr=0x%08x\n", (unsigned)hr); return 2; }
    unsigned char bytes[64] = {};
    ctx->UpdateSubresource(buf, 0, nullptr, bytes, 0, 0);
    put("R", "UpdateSubresource returned");  // must not be reached
  } else {
    fprintf(stderr, "unknown case %s\n", what.c_str());
    return 2;
  }
  ctx->Release();
  dev->Release();
  return 0;  // through exit(): the converter's line on standard output is flushed by LLVM at exit
}

// ---- the parent --------------------------------------------------------------------------------------------
static std::string drain(int fd) {
  std::string s;
  char buf[4096];
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0) s.append(buf, n);
  close(fd);
  return s;
}

struct Result {
  std::vector<std::string> rec;  // what the two functions and the child wrote into the pipe, in order
  std::string err, out;          // the child's stderr and stdout
  int status = 0;
  int find(const std::string &tag, const std::string &needle, int from = 0) const {
    for (size_t i = from; i < rec.size(); i++)
      if (rec[i].compare(0, tag.size() + 1, tag + " ") == 0 && rec[i].find(needle) != std::string::npos) return (int)i;
    return -1;
  }
  int count(const std::string &tag) const {
    int n = 0;
    for (auto &r : rec) if (r.compare(0, tag.size() + 1, tag + " ") == 0) n++;
    return n;
  }
  bool dxmt_wrote(const char *prefix) const {  // a line of DXMT's own logger on the child's stderr
    for (size_t at = 0; at < err.size();) {
      size_t nl = err.find('\n', at);
      if (err.compare(at, strlen(prefix), prefix) == 0) return true;
      if (nl == std::string::npos) break;
      at = nl + 1;
    }
    return false;
  }
};

static Result run(const std::string &dir, const std::string &what) {
  int p[2], pe[2], po[2];
  if (pipe(p) || pipe(pe) || pipe(po)) { perror("pipe"); exit(2); }
  fflush(stdout); fflush(stderr);
  pid_t pid = fork();
  if (pid == 0) {
    close(p[0]); close(pe[0]); close(po[0]);
    g_pipe = p[1];
    dup2(pe[1], 2); dup2(po[1], 1);
    close(pe[1]); close(po[1]);
    exit(child(dir, what));
  }
  close(p[1]); close(pe[1]); close(po[1]);
  Result res;
  std::string all = drain(p[0]);  // the child writes little: nothing can fill a pipe while we read this one
  res.err = drain(pe[0]);
  res.out = drain(po[0]);
  waitpid(pid, &res.status, 0);
  for (size_t at = 0; at < all.size();) {
    size_t nl = all.find('\n', at);
    if (nl == std::string::npos) nl = all.size();
    res.rec.push_back(all.substr(at, nl - at));
    at = nl + 1;
  }
  return res;
}

static int g_checks = 0, g_failed = 0;
static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) g_failed++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
}

static void show(const char *name, const Result &r) {
  printf("\n== %s ==\n", name);
  if (WIFSIGNALED(r.status)) printf("  child: killed by signal %d (%s)\n", WTERMSIG(r.status), strsignal(WTERMSIG(r.status)));
  else printf("  child: exit status %d\n", WEXITSTATUS(r.status));
  for (auto &x : r.rec) {
    bool quiet = x.compare(0, 3, "L2 ") == 0 || x.compare(0, 3, "L3 ") == 0;  // info and warn: counted, not printed
    if (!quiet) printf("  heard: %.300s\n", x.c_str());
  }
  printf("  heard %d info, %d warn, %d error line(s), %d abort message(s), %d SIGABRT; bad user pointers: %d\n", r.count("L2"), r.count("L3"),
         r.count("L4"), r.count("A"), r.count("S"), r.count("BADUSER"));
}

int main(int argc, char **argv) {
  std::string dir, kind;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--kind") && i + 1 < argc) kind = argv[++i];
    else if (dir.empty()) dir = argv[i];
    else dir.clear(), i = argc;
  }
  if (dir.empty() || (kind != "release" && kind != "assert")) {
    fprintf(stderr, "usage: %s <DXMT meson build dir> --kind release|assert\n", argv[0]);
    return 2;
  }
  {
    Result r = run(dir, "error-line");
    show("error-line: a shader container with no code", r);
    check(WIFEXITED(r.status) && WEXITSTATUS(r.status) == 0, "the child ended normally");
    int e = r.find("L4", "Failed to initialize shader"), a = r.find("R", "");
    check(e >= 0, "the log function got an error-level line \"Failed to initialize shader ...\"");
    check(a >= 0 && r.rec[a].compare(0, 4, "R 0x") == 0 && r.rec[a][4] >= '8' && r.rec[a].find("shader=null") != std::string::npos,
          "CreatePixelShader failed and gave no object");
    check(e >= 0 && a > e, "the line was heard before the call returned");
    check(r.count("L2") >= 1, "info-level lines are heard too (default level)");
    check(r.dxmt_wrote("err:   Failed to initialize shader"), "DXMT also wrote the line to standard error (answer 0 = before, not instead)");
    check(r.count("BADUSER") == 0 && r.count("A") == 0, "the user pointer came back; the abort function was not called");
  }
  {
    Result r = run(dir, "instead");
    show("instead: the log function answers 1", r);
    check(WIFEXITED(r.status) && WEXITSTATUS(r.status) == 0, "the child ended normally");
    check(r.find("L4", "Failed to initialize shader") >= 0 && r.count("L2") >= 1, "the log function got the lines");
    check(!r.dxmt_wrote("err:") && !r.dxmt_wrote("warn:") && !r.dxmt_wrote("info:"), "DXMT wrote none of them to standard error");
  }
  {
    Result r = run(dir, "level-none");
    show("level-none: DXMT_LOG_LEVEL=none", r);
    check(WIFEXITED(r.status) && WEXITSTATUS(r.status) == 0, "the child ended normally");
    check(r.find("L4", "Failed to initialize shader") >= 0, "the log function still got the error line");
    check(r.count("L2") == 0 && r.count("L3") == 0, "and nothing below error level");
    check(!r.dxmt_wrote("err:") && !r.dxmt_wrote("warn:") && !r.dxmt_wrote("info:"), "DXMT wrote nothing to standard error");
  }
  {
    Result r = run(dir, "map-null");
    show("map-null: Map of a default-usage buffer, null out-pointer", r);
    check(WIFEXITED(r.status) && WEXITSTATUS(r.status) == 0, "the child ended normally: this Map does not reach UNIMPLEMENTED");
    check(r.find("R", "0x80070057") >= 0, "Map returned E_INVALIDARG");
    check(r.count("A") == 0, "the abort function was not called");
  }
  {
    Result r = run(dir, "unimplemented");
    show("unimplemented: UpdateSubresource into a staging buffer", r);
    int e = r.find("L4", "update buffer: staging"), a = r.find("A", "update buffer: staging");
    check(e >= 0, "the log function got the error line");
    check(a >= 0, "the abort function got the message");
    check(e >= 0 && a > e, "in that order");
    check(r.find("R", "") < 0, "UpdateSubresource did not return");
    check(a >= 0 && r.find("S", "SIGABRT") > a && WIFEXITED(r.status) && WEXITSTATUS(r.status) == 86,
          "then DXMT called abort(): the child's SIGABRT handler ran, after the abort function");
    check(r.count("BADUSER") == 0, "the user pointers came back");
  }
  {
    Result r = run(dir, "abort-exits");
    show("abort-exits: the abort function ends the process itself", r);
    check(r.find("A", "update buffer: staging") >= 0, "the abort function got the message");
    check(WIFEXITED(r.status) && WEXITSTATUS(r.status) == 77 && r.count("S") == 0, "the child ended with the function's own status 77, no SIGABRT");
  }
  {
    Result r = run(dir, "unknown-op");
    show("unknown-op: a pixel shader with an instruction the converter has no case for", r);
    if (kind == "assert") {
      int a = r.find("A", "unhandled dxbc instruction");
      check(a >= 0 && r.rec[a].find("Assertion failed") != std::string::npos, "assert build: the abort function got the converter's failed assertion");
      check(r.find("R", "") < 0, "CreatePixelShader did not return");
      check(a >= 0 && r.find("S", "SIGABRT") > a && WIFEXITED(r.status) && WEXITSTATUS(r.status) == 86,
            "then abort(): the child's SIGABRT handler ran, after the abort function");
    } else {
      check(WIFEXITED(r.status) && WEXITSTATUS(r.status) == 0, "release build: the child ended normally");
      check(r.find("R", "0x00000000 shader=non-null") >= 0, "release build: CreatePixelShader succeeded");
      check(r.count("L4") == 0 && r.count("A") == 0, "release build: neither function heard anything about it (the known hole)");
      check(r.out.find("unhandled dxbc instruction 207") != std::string::npos, "release build: the only trace is \"unhandled dxbc instruction 207\" on standard output");
    }
  }

  printf("\n%d checks, %d failed (%s build)\n", g_checks, g_failed, kind.c_str());
  printf(g_failed ? "HOOK_TEST_FAIL\n" : "HOOK_TEST_OK\n");
  return g_failed ? 1 : 0;
}
